-- SPDX-License-Identifier: Apache-2.0
--
-- Quotient schema. Copied verbatim from docs/quotient-guide.md §15.1.
-- Run automatically by the postgres container on first start (mounted read-only
-- at /docker-entrypoint-initdb.d, see deploy/docker-compose.yml). Runs only when
-- the data directory is empty: `docker compose down -v` to re-run from scratch.
-- Never edit an applied migration; add a new numbered file instead.

CREATE TABLE tenants (
  id            UUID PRIMARY KEY DEFAULT gen_random_uuid(),
  slug          TEXT NOT NULL UNIQUE CHECK (slug ~ '^[a-z0-9-]{2,40}$'),
  display_name  TEXT NOT NULL,
  created_at    TIMESTAMPTZ NOT NULL DEFAULT now()
);

CREATE TABLE api_keys (
  id          UUID PRIMARY KEY DEFAULT gen_random_uuid(),
  tenant_id   UUID NOT NULL REFERENCES tenants(id) ON DELETE CASCADE,
  key_hash    BYTEA NOT NULL UNIQUE,     -- SHA-256 of the secret; never plaintext
  prefix      TEXT NOT NULL,             -- first characters, for display only
  label       TEXT NOT NULL DEFAULT '',
  created_at  TIMESTAMPTZ NOT NULL DEFAULT now(),
  revoked_at  TIMESTAMPTZ
);
CREATE INDEX api_keys_tenant_idx ON api_keys (tenant_id);

CREATE TYPE algorithm    AS ENUM ('fixed_window', 'gcra', 'leased');
CREATE TYPE failure_mode AS ENUM ('fail_open', 'fail_closed', 'local_fallback');
CREATE TYPE rate_unit    AS ENUM ('second', 'minute', 'hour', 'day');

CREATE TABLE policies (
  id                 UUID PRIMARY KEY DEFAULT gen_random_uuid(),
  name               TEXT NOT NULL UNIQUE,
  domain             TEXT NOT NULL,
  match              JSONB NOT NULL,   -- [{"key":"tenant","value":"acme"}, ...]
  match_signature    TEXT NOT NULL,    -- canonical form, e.g. 'tenant=*|route=orders'
  algorithm          algorithm NOT NULL,
  requests_per_unit  INTEGER NOT NULL CHECK (requests_per_unit > 0),
  unit               rate_unit NOT NULL,
  burst              INTEGER CHECK (burst IS NULL OR burst > 0),
  failure_mode       failure_mode NOT NULL DEFAULT 'fail_open',
  shadow             BOOLEAN NOT NULL DEFAULT false,
  version            BIGINT NOT NULL DEFAULT 1,
  updated_at         TIMESTAMPTZ NOT NULL DEFAULT now(),
  UNIQUE (domain, match_signature)     -- two policies can never match identically
);

-- Usage is historical fact: keyed by slug (no FK) so it survives tenant
-- deletion and can record the 'anonymous' pseudo-tenant.
CREATE TABLE usage_hourly (
  tenant_slug  TEXT NOT NULL,
  hour         TIMESTAMPTZ NOT NULL,
  policy_name  TEXT NOT NULL,
  allowed      BIGINT NOT NULL DEFAULT 0,
  limited      BIGINT NOT NULL DEFAULT 0,
  PRIMARY KEY (tenant_slug, hour, policy_name)
);

CREATE TABLE audit_log (
  id      BIGSERIAL PRIMARY KEY,
  actor   TEXT NOT NULL,
  action  TEXT NOT NULL,               -- e.g. 'policy.put', 'api_key.revoke'
  entity  TEXT NOT NULL,
  before  JSONB,
  after   JSONB,
  at      TIMESTAMPTZ NOT NULL DEFAULT now()
);

-- Single-row configuration version, bumped on every config change.
CREATE TABLE config_state (
  id       BOOLEAN PRIMARY KEY DEFAULT true CHECK (id),
  version  BIGINT NOT NULL
);
INSERT INTO config_state VALUES (true, 1);

CREATE FUNCTION notify_config_change() RETURNS trigger AS $$
DECLARE v BIGINT;
BEGIN
  UPDATE config_state SET version = version + 1 RETURNING version INTO v;
  PERFORM pg_notify('quotient_config', v::text);
  RETURN NULL;
END;
$$ LANGUAGE plpgsql;

CREATE TRIGGER policies_changed AFTER INSERT OR UPDATE OR DELETE ON policies
  FOR EACH STATEMENT EXECUTE FUNCTION notify_config_change();
CREATE TRIGGER api_keys_changed AFTER INSERT OR UPDATE OR DELETE ON api_keys
  FOR EACH STATEMENT EXECUTE FUNCTION notify_config_change();
CREATE TRIGGER tenants_changed AFTER INSERT OR UPDATE OR DELETE ON tenants
  FOR EACH STATEMENT EXECUTE FUNCTION notify_config_change();

-- Note: NOTIFY is transactional -- listeners only see it once this script's
-- implicit transaction commits, and never if it rolls back. gen_random_uuid()
-- is built into PostgreSQL 13+; sha256() (used by 0002_seed.sql) is built into
-- PostgreSQL 14+. We run postgres:17, so both are available with no extension.
