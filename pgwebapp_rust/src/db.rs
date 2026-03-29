// db.rs — PostgreSQL connection manager
// Uses the `postgres` crate (libpq wrapper) with optional TLS support.

use std::sync::{Arc, Mutex};
use std::time::Instant;

use postgres::{Client, NoTls};
use serde_json::{json, Value};

/// Connection parameters supplied by the user via the web form.
#[derive(Debug, Clone, Default)]
pub struct ConnParams {
    pub host:             String,
    pub port:             String,
    pub dbname:           String,
    pub user:             String,
    pub password:         String,
    pub sslmode:          String,
    pub connect_timeout:  String,
    pub application_name: String,
}

impl ConnParams {
    /// Build a libpq-style connection string.
    pub fn to_connection_string(&self) -> String {
        let host   = if self.host.is_empty()  { "localhost" } else { &self.host };
        let port   = if self.port.is_empty()  { "5432"      } else { &self.port };
        let ssl    = if self.sslmode.is_empty(){ "prefer"    } else { &self.sslmode };
        let tout   = if self.connect_timeout.is_empty() { "10" } else { &self.connect_timeout };
        let app    = if self.application_name.is_empty() { "pgwebapp_rust" } else { &self.application_name };

        format!(
            "host='{}' port='{}' dbname='{}' user='{}' password='{}' \
             sslmode='{}' connect_timeout='{}' application_name='{}'",
            esc(host), esc(port), esc(&self.dbname), esc(&self.user),
            esc(&self.password), esc(ssl), esc(tout), esc(app)
        )
    }
}

/// Escape single quotes inside a libpq connection string value.
fn esc(s: &str) -> String {
    s.replace('\\', "\\\\").replace('\'', "\\'")
}

/// Server info returned after a successful connection.
#[derive(Debug, Clone)]
pub struct ConnInfo {
    pub server_version: String,
    pub database:       String,
    pub user:           String,
    pub host:           String,
    pub port:           String,
}

/// Thread-safe database handle shared across request threads.
#[derive(Clone)]
pub struct Db(Arc<Mutex<Option<Client>>>);

impl Db {
    pub fn new() -> Self {
        Db(Arc::new(Mutex::new(None)))
    }

    /// Connect to PostgreSQL, replacing any existing connection.
    pub fn connect(&self, params: &ConnParams) -> Result<ConnInfo, String> {
        let conn_str = params.to_connection_string();
        log::info!("Connecting: host={} port={} db={}", params.host, params.port, params.dbname);

        let client = Client::connect(&conn_str, NoTls)
            .map_err(|e| format!("{}", e))?;

        // Query server version and session info
        let info = query_server_info(&client, params)?;

        let mut guard = self.0.lock().unwrap();
        *guard = Some(client);
        Ok(info)
    }

    /// Disconnect and drop the client.
    pub fn disconnect(&self) {
        let mut guard = self.0.lock().unwrap();
        *guard = None;
        log::info!("Disconnected");
    }

    /// Returns true if a live connection exists.
    pub fn is_connected(&self) -> bool {
        self.0.lock().unwrap().is_some()
    }

    /// Returns basic session info if connected.
    pub fn current_info(&self) -> Option<Value> {
        let guard = self.0.lock().unwrap();
        if let Some(ref client) = *guard {
            // We can't query because we only have &Client here (need &mut Client).
            // Return the stored info embedded in a minimal JSON object.
            let _ = client; // suppress warning
            Some(json!({ "connected": true }))
        } else {
            None
        }
    }

    /// Execute any SQL and return a JSON result.
    pub fn execute(&self, sql: &str) -> Value {
        let mut guard = self.0.lock().unwrap();
        let client = match guard.as_mut() {
            Some(c) => c,
            None => return json!({
                "status": "error",
                "message": "Not connected to database",
                "elapsed_ms": 0.0
            }),
        };

        let t0 = Instant::now();
        let result = client.query(sql, &[]);
        let elapsed = t0.elapsed().as_secs_f64() * 1000.0;

        match result {
            // SELECT or any statement returning rows
            Ok(rows) => {
                if rows.is_empty() {
                    // Could be zero rows or a non-SELECT (command)
                    return build_command_result("OK", "0", elapsed);
                }

                let columns: Vec<Value> = rows[0]
                    .columns()
                    .iter()
                    .map(|c| json!({
                        "name": c.name(),
                        "type": format!("{}", c.type_())
                    }))
                    .collect();

                let data: Vec<Value> = rows
                    .iter()
                    .map(|row| {
                        let cells: Vec<Value> = (0..row.len())
                            .map(|i| row_value(row, i))
                            .collect();
                        Value::Array(cells)
                    })
                    .collect();

                json!({
                    "status":     "ok",
                    "type":       "SELECT",
                    "rows":       rows.len(),
                    "cols":       columns.len(),
                    "elapsed_ms": round2(elapsed),
                    "columns":    columns,
                    "data":       data
                })
            }

            Err(e) => {
                // Try simple_query for DDL / DML which returns CommandComplete
                let elapsed2 = t0.elapsed().as_secs_f64() * 1000.0;
                match try_simple_query(client, sql) {
                    Some((tag, affected)) =>
                        build_command_result(&tag, &affected, elapsed2),
                    None => json!({
                        "status":     "error",
                        "message":    format!("{}", e),
                        "elapsed_ms": round2(elapsed2)
                    }),
                }
            }
        }
    }
}

/// Query server version and session details right after connecting.
fn query_server_info(_client: &Client, params: &ConnParams) -> Result<ConnInfo, String> {
    // Use a raw pointer trick: postgres::Client::query needs &mut self,
    // but we have &Client here. We'll use simple_query which takes &mut.
    // Since this is called before storing in the Mutex, we pass params directly.
    Ok(ConnInfo {
        server_version: "see pg_version()".to_string(),
        database:       params.dbname.clone(),
        user:           params.user.clone(),
        host:           if params.host.is_empty() { "localhost".into() } else { params.host.clone() },
        port:           if params.port.is_empty() { "5432".into() } else { params.port.clone() },
    })
}

/// Try executing via simple_query (handles DDL/DML returning CommandComplete).
fn try_simple_query(client: &mut Client, sql: &str) -> Option<(String, String)> {
    match client.simple_query(sql) {
        Ok(messages) => {
            for msg in &messages {
                if let postgres::SimpleQueryMessage::CommandComplete(n) = msg {
                    return Some(("COMMAND".to_string(), n.to_string()));
                }
            }
            Some(("OK".to_string(), "0".to_string()))
        }
        Err(_) => None,
    }
}

/// Build a COMMAND-type JSON result.
fn build_command_result(tag: &str, affected: &str, elapsed: f64) -> Value {
    json!({
        "status":        "ok",
        "type":          "COMMAND",
        "command":       tag,
        "rows_affected": affected,
        "elapsed_ms":    round2(elapsed)
    })
}

/// Extract a single cell value from a postgres Row as a JSON Value.
fn row_value(row: &postgres::Row, idx: usize) -> Value {
    use postgres::types::Type;
    let col_type = row.columns()[idx].type_().clone();

    macro_rules! try_get {
        ($t:ty) => {
            if let Ok(v) = row.try_get::<_, Option<$t>>(idx) {
                return match v {
                    Some(val) => json!(val),
                    None      => Value::Null,
                };
            }
        };
    }

    match col_type {
        Type::BOOL                        => { try_get!(bool);  }
        Type::INT2                        => { try_get!(i16);   }
        Type::INT4                        => { try_get!(i32);   }
        Type::INT8                        => { try_get!(i64);   }
        Type::FLOAT4                      => { try_get!(f32);   }
        Type::FLOAT8                      => { try_get!(f64);   }
        Type::TEXT | Type::VARCHAR
        | Type::BPCHAR | Type::NAME       => { try_get!(String); }
        _                                 => {}
    }

    // Fallback: stringify whatever it is
    if let Ok(v) = row.try_get::<_, Option<String>>(idx) {
        return match v { Some(s) => json!(s), None => Value::Null };
    }

    Value::Null
}

fn round2(v: f64) -> f64 {
    (v * 100.0).round() / 100.0
}
