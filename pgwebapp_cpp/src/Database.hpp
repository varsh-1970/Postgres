#pragma once
/*
 * Database.hpp — PostgreSQL connection manager (C++ / libpq)
 * Wraps PGconn in RAII; thread-safe via std::mutex.
 */

#include <string>
#include <vector>
#include <stdexcept>
#include <mutex>
#include <cstring>
#include <libpq-fe.h>

/* ── Column metadata ──────────────────────────────────────── */
struct Column {
    std::string name;
    unsigned int oid;
};

/* ── A single query result ────────────────────────────────── */
struct QueryResult {
    enum class Type { SELECT, COMMAND, ERROR };

    Type        type;
    std::string error_message;
    std::string command_tag;
    std::string rows_affected;
    double      elapsed_ms = 0.0;

    std::vector<Column>                   columns;
    std::vector<std::vector<std::string>> rows;        // "" == NULL handled via nulls
    std::vector<std::vector<bool>>        null_flags;  // true = NULL cell
};

/* ── Connection parameters ────────────────────────────────── */
struct ConnParams {
    std::string host             = "localhost";
    std::string port             = "5432";
    std::string dbname;
    std::string user;
    std::string password;
    std::string sslmode          = "prefer";
    std::string connect_timeout  = "10";
    std::string application_name = "pgwebapp_cpp";
};

/* ── ConnInfo returned after a successful connect ─────────── */
struct ConnInfo {
    std::string server_version;
    std::string database;
    std::string user;
    std::string host;
    std::string port;
};

/* ─────────────────────────────────────────────────────────── */
class Database {
public:
    Database() = default;
    ~Database() { disconnect(); }

    /* Not copyable, moveable only */
    Database(const Database&)            = delete;
    Database& operator=(const Database&) = delete;

    /* Connect — throws std::runtime_error on failure */
    ConnInfo connect(const ConnParams& p) {
        std::lock_guard<std::mutex> lk(mutex_);

        if (conn_) { PQfinish(conn_); conn_ = nullptr; }

        /* Build libpq connection string */
        std::string cs =
            "host='"             + escape_sq(p.host)             + "' "
            "port='"             + escape_sq(p.port)             + "' "
            "dbname='"           + escape_sq(p.dbname)           + "' "
            "user='"             + escape_sq(p.user)             + "' "
            "password='"         + escape_sq(p.password)         + "' "
            "sslmode='"          + escape_sq(p.sslmode)          + "' "
            "connect_timeout='"  + escape_sq(p.connect_timeout)  + "' "
            "application_name='" + escape_sq(p.application_name) + "'";

        conn_ = PQconnectdb(cs.c_str());

        if (PQstatus(conn_) != CONNECTION_OK) {
            std::string err = PQerrorMessage(conn_);
            PQfinish(conn_); conn_ = nullptr;
            throw std::runtime_error(err);
        }

        /* Collect server info */
        ConnInfo info;
        int sv = PQserverVersion(conn_);
        info.server_version = std::to_string(sv / 10000) + "."
                            + std::to_string((sv % 10000) / 100) + "."
                            + std::to_string(sv % 100);
        info.database = safe_str(PQdb(conn_));
        info.user     = safe_str(PQuser(conn_));
        info.host     = safe_str(PQhost(conn_));
        info.port     = safe_str(PQport(conn_));
        return info;
    }

    void disconnect() {
        std::lock_guard<std::mutex> lk(mutex_);
        if (conn_) { PQfinish(conn_); conn_ = nullptr; }
    }

    bool is_connected() const {
        std::lock_guard<std::mutex> lk(mutex_);
        return conn_ && PQstatus(conn_) == CONNECTION_OK;
    }

    /* Returns basic info if connected, empty struct otherwise */
    ConnInfo current_info() const {
        std::lock_guard<std::mutex> lk(mutex_);
        if (!conn_ || PQstatus(conn_) != CONNECTION_OK) return {};
        ConnInfo info;
        info.database = safe_str(PQdb(conn_));
        info.user     = safe_str(PQuser(conn_));
        info.host     = safe_str(PQhost(conn_));
        info.port     = safe_str(PQport(conn_));
        return info;
    }

    /* Execute SQL — thread-safe */
    QueryResult execute(const std::string& sql) {
        std::lock_guard<std::mutex> lk(mutex_);

        QueryResult result;

        if (!conn_ || PQstatus(conn_) != CONNECTION_OK) {
            result.type          = QueryResult::Type::ERROR;
            result.error_message = "Not connected to database";
            return result;
        }

        PQreset(conn_);   /* reconnect if connection dropped */

        /* Time the query */
        struct timespec t0, t1;
        clock_gettime(CLOCK_MONOTONIC, &t0);
        PGresult* res = PQexec(conn_, sql.c_str());
        clock_gettime(CLOCK_MONOTONIC, &t1);

        result.elapsed_ms = (t1.tv_sec - t0.tv_sec) * 1000.0
                          + (t1.tv_nsec - t0.tv_nsec) / 1.0e6;

        ExecStatusType st = PQresultStatus(res);

        if (st == PGRES_TUPLES_OK) {
            result.type = QueryResult::Type::SELECT;
            int nrows   = PQntuples(res);
            int ncols   = PQnfields(res);

            result.columns.resize(ncols);
            for (int c = 0; c < ncols; ++c) {
                result.columns[c].name = safe_str(PQfname(res, c));
                result.columns[c].oid  = static_cast<unsigned int>(PQftype(res, c));
            }

            result.rows.resize(nrows, std::vector<std::string>(ncols));
            result.null_flags.resize(nrows, std::vector<bool>(ncols, false));

            for (int r = 0; r < nrows; ++r) {
                for (int c = 0; c < ncols; ++c) {
                    if (PQgetisnull(res, r, c)) {
                        result.null_flags[r][c] = true;
                    } else {
                        result.rows[r][c] = safe_str(PQgetvalue(res, r, c));
                    }
                }
            }

        } else if (st == PGRES_COMMAND_OK) {
            result.type         = QueryResult::Type::COMMAND;
            result.command_tag  = safe_str(PQcmdStatus(res));
            result.rows_affected= safe_str(PQcmdTuples(res));

        } else {
            result.type          = QueryResult::Type::ERROR;
            result.error_message = safe_str(PQresultErrorMessage(res));
        }

        PQclear(res);
        return result;
    }

private:
    mutable std::mutex mutex_;
    PGconn*            conn_ = nullptr;

    static std::string safe_str(const char* s) {
        return s ? std::string(s) : std::string{};
    }

    /* Escape single quotes inside a libpq connection string value */
    static std::string escape_sq(const std::string& s) {
        std::string out;
        out.reserve(s.size());
        for (char c : s) {
            if (c == '\'') out += "\\'";
            else if (c == '\\') out += "\\\\";
            else out += c;
        }
        return out;
    }
};
