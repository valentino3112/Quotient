# SPDX-License-Identifier: Apache-2.0
#
# Convenience wrappers around the commands in deploy/docker-compose.yml and
# HANDOFF-person-b.md §5. Recipe lines must start with a real tab character.

COMPOSE := docker compose -f deploy/docker-compose.yml

.PHONY: up down ps logs-quotient smoke

up:          ## build images and start the stack in the background
	$(COMPOSE) up -d --build

down:        ## stop everything and delete volumes (re-runs the SQL migrations next time)
	$(COMPOSE) down -v

ps:          ## show container status and health
	$(COMPOSE) ps

logs-quotient: ## tail Quotient's logs (SPDLOG_LEVEL=debug logs one line per RLS call)
	$(COMPOSE) logs -f quotient

smoke:       ## the "walking skeleton" milestone from HANDOFF-person-b.md §5
	@echo "--- anonymous request (no x-api-key) ---"
	curl -i localhost:8080/v1/items
	@echo "\n--- request with a demo key ---"
	curl -i -H 'x-api-key: qk_demo_acme' localhost:8080/v1/items
	@echo "\n--- ratelimit stats (ratelimit.ok should rise, ratelimit.error should stay 0) ---"
	curl -s localhost:9901/stats | grep ratelimit
