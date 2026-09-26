-- SPDX-License-Identifier: Apache-2.0
--
-- Demo seed data. Copied verbatim from docs/quotient-guide.md §15.2.
-- Runs right after 0001_init.sql (alphabetical order in /docker-entrypoint-initdb.d).

INSERT INTO tenants (slug, display_name) VALUES
  ('acme', 'Acme Corp'), ('globex', 'Globex'), ('initech', 'Initech');

-- Demo keys: qk_demo_acme, qk_demo_globex, qk_demo_initech
INSERT INTO api_keys (tenant_id, key_hash, prefix, label)
SELECT id, sha256(convert_to('qk_demo_' || slug, 'UTF8')), 'qk_demo_', 'demo key'
FROM tenants;

INSERT INTO policies (name, domain, match, match_signature, algorithm,
                      requests_per_unit, unit, burst, failure_mode) VALUES
  ('tenant-default', 'api', '[{"key":"tenant","value":""}]', 'tenant=*',
   'gcra', 100, 'second', 100, 'fail_open'),
  ('tenant-globex', 'api', '[{"key":"tenant","value":"globex"}]', 'tenant=globex',
   'gcra', 20, 'second', 20, 'fail_open'),
  ('anonymous', 'api', '[{"key":"tenant","value":"anonymous"}]', 'tenant=anonymous',
   'fixed_window', 10, 'minute', NULL, 'fail_closed'),
  ('orders-per-tenant', 'api',
   '[{"key":"tenant","value":""},{"key":"route","value":"orders"}]',
   'tenant=*|route=orders', 'gcra', 50, 'second', 50, 'local_fallback');

-- Gives the demo three behaviours to show: a generous default, a small customer
-- (Globex) that hits its limit easily, and a strict fail-closed policy for
-- anonymous traffic.
