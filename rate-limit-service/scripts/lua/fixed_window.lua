-- SPDX-License-Identifier: Apache-2.0
-- Fixed window stored in ONE hash key {start, count}, so the script never
-- touches a key it was not given (required for Redis Cluster).
-- KEYS[1] bucket key; ARGV[1] limit; ARGV[2] window_ms; ARGV[3] cost
-- Returns {allowed (0|1), remaining, reset_after_ms}
local t = redis.call('TIME')
local now_ms = tonumber(t[1]) * 1000 + math.floor(tonumber(t[2]) / 1000)
local limit = tonumber(ARGV[1])
local window = tonumber(ARGV[2])
local cost = tonumber(ARGV[3])
local start = now_ms - (now_ms % window)

local stored = redis.call('HMGET', KEYS[1], 'start', 'count')
local count = 0
if tonumber(stored[1]) == start then count = tonumber(stored[2]) end

local reset_after = start + window - now_ms
if count + cost > limit then
  return {0, limit - count, reset_after}
end
count = count + cost
redis.call('HSET', KEYS[1], 'start', string.format('%.0f', start), 'count', count)
redis.call('PEXPIRE', KEYS[1], reset_after)
return {1, limit - count, reset_after}
