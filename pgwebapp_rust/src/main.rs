// main.rs — pgwebapp Rust backend
//
// Stack:
//   tiny_http  — embedded HTTP/1.1 server (no async runtime needed)
//   postgres   — PostgreSQL client (sync, wraps libpq)
//   serde_json — JSON serialization
//
// Build:  cargo build --release
// Run:    ./target/release/pgwebapp [port]   (default: 8080)

mod db;
mod handlers;

use std::env;
use std::io::Cursor;
use std::sync::Arc;

use tiny_http::{Header, Method, Request, Response, Server, StatusCode};

use db::Db;

// Embed the web UI HTML at compile time — zero runtime file I/O.
const HTML: &str = include_str!("index.html");

fn main() {
    // Initialise logger (RUST_LOG=info ./pgwebapp to see logs)
    env_logger::Builder::from_env(
        env_logger::Env::default().default_filter_or("info"),
    )
    .init();

    let port: u16 = env::args()
        .nth(1)
        .and_then(|s| s.parse().ok())
        .unwrap_or(8080);

    let addr = format!("0.0.0.0:{}", port);
    let server = Server::http(&addr).expect("Failed to bind server");

    // Shared database handle (Arc so it can be cloned into threads)
    let db = Arc::new(Db::new());

    println!();
    println!("  ╔══════════════════════════════════════════╗");
    println!("  ║   pgwebapp Rust — PostgreSQL Studio      ║");
    println!("  ╠══════════════════════════════════════════╣");
    println!("  ║  Server : http://localhost:{:<13}║", port);
    println!("  ║  Backend: Rust + postgres crate          ║");
    println!("  ║  Press Ctrl+C to stop                    ║");
    println!("  ╚══════════════════════════════════════════╝");
    println!();

    // Request loop — tiny_http handles the TCP layer
    for request in server.incoming_requests() {
        let db = Arc::clone(&db);
        std::thread::spawn(move || handle_request(request, db));
    }
}

/// Dispatch a single HTTP request.
fn handle_request(mut request: Request, db: Arc<Db>) {
    let method = request.method().clone();
    let path   = request.url().split('?').next().unwrap_or("/").to_owned();

    log::info!("{} {}", method, path);

    match (method, path.as_str()) {
        // ── Web UI ────────────────────────────────────────────────────
        (Method::Get, "/") | (Method::Get, "/index.html") => {
            respond_html(request, HTML);
        }

        // ── API ───────────────────────────────────────────────────────
        (Method::Post, "/api/connect") => {
            let body = read_body(&mut request);
            let json = handlers::handle_connect(&body, &db);
            respond_json(request, &json);
        }

        (Method::Post, "/api/disconnect") => {
            let json = handlers::handle_disconnect(&db);
            respond_json(request, &json);
        }

        (Method::Get, "/api/status") => {
            let json = handlers::handle_status(&db);
            respond_json(request, &json);
        }

        (Method::Post, "/api/query") => {
            let body = read_body(&mut request);
            let json = handlers::handle_query(&body, &db);
            respond_json(request, &json);
        }

        // ── 404 ───────────────────────────────────────────────────────
        _ => {
            let body = r#"{"error":"Not Found"}"#;
            let response = Response::new(
                StatusCode(404),
                vec![
                    content_type("application/json"),
                    cors_header(),
                ],
                Cursor::new(body.as_bytes().to_vec()),
                Some(body.len()),
                None,
            );
            let _ = request.respond(response);
        }
    }
}

// ── Helpers ───────────────────────────────────────────────────────────────────

fn read_body(request: &mut Request) -> String {
    let mut body = String::new();
    let _ = request.as_reader().read_to_string(&mut body);
    body
}

fn respond_html(request: Request, html: &str) {
    let bytes = html.as_bytes().to_vec();
    let len   = bytes.len();
    let response = Response::new(
        StatusCode(200),
        vec![
            content_type("text/html; charset=utf-8"),
            cors_header(),
        ],
        Cursor::new(bytes),
        Some(len),
        None,
    );
    let _ = request.respond(response);
}

fn respond_json(request: Request, value: &serde_json::Value) {
    let body  = value.to_string();
    let bytes = body.as_bytes().to_vec();
    let len   = bytes.len();
    let response = Response::new(
        StatusCode(200),
        vec![
            content_type("application/json; charset=utf-8"),
            cors_header(),
        ],
        Cursor::new(bytes),
        Some(len),
        None,
    );
    let _ = request.respond(response);
}

fn content_type(ct: &str) -> Header {
    Header::from_bytes("Content-Type", ct).unwrap()
}

fn cors_header() -> Header {
    Header::from_bytes("Access-Control-Allow-Origin", "*").unwrap()
}
