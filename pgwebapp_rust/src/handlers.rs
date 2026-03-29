// handlers.rs — Route logic for each API endpoint

use serde_json::{json, Value};
use std::collections::HashMap;

use crate::db::{ConnParams, Db};

/// Parse application/x-www-form-urlencoded body into a HashMap.
pub fn parse_form(body: &str) -> HashMap<String, String> {
    body.split('&')
        .filter_map(|pair| {
            let mut parts = pair.splitn(2, '=');
            let key = parts.next()?;
            let val = parts.next().unwrap_or("");
            let k = urlencoding::decode(key).unwrap_or_default().replace('+', " ");
            let v = urlencoding::decode(val).unwrap_or_default().replace('+', " ");
            Some((k, v))
        })
        .collect()
}

fn field<'a>(map: &'a HashMap<String, String>, key: &str, default: &'a str) -> String {
    map.get(key)
       .filter(|v| !v.is_empty())
       .map(|v| v.as_str())
       .unwrap_or(default)
       .to_string()
}

/// POST /api/connect
pub fn handle_connect(body: &str, db: &Db) -> Value {
    let f = parse_form(body);

    let params = ConnParams {
        host:             field(&f, "host",             "localhost"),
        port:             field(&f, "port",             "5432"),
        dbname:           field(&f, "dbname",           ""),
        user:             field(&f, "user",             ""),
        password:         field(&f, "password",         ""),
        sslmode:          field(&f, "sslmode",          "prefer"),
        connect_timeout:  field(&f, "connect_timeout",  "10"),
        application_name: field(&f, "application_name", "pgwebapp_rust"),
    };

    match db.connect(&params) {
        Ok(info) => json!({
            "status":         "ok",
            "message":        "Connected successfully",
            "server_version": info.server_version,
            "database":       info.database,
            "user":           info.user,
            "host":           info.host,
            "port":           info.port
        }),
        Err(e) => json!({
            "status":  "error",
            "message": e
        }),
    }
}

/// POST /api/disconnect
pub fn handle_disconnect(db: &Db) -> Value {
    db.disconnect();
    json!({ "status": "ok", "message": "Disconnected" })
}

/// GET /api/status
pub fn handle_status(db: &Db) -> Value {
    if db.is_connected() {
        json!({ "connected": true })
    } else {
        json!({ "connected": false })
    }
}

/// POST /api/query
pub fn handle_query(body: &str, db: &Db) -> Value {
    let f = parse_form(body);
    let sql = field(&f, "query", "");

    if sql.trim().is_empty() {
        return json!({ "status": "error", "message": "Empty query" });
    }

    db.execute(&sql)
}
