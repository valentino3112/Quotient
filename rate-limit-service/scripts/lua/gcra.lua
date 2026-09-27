-- SPDX-License-Identifier: Apache-2.0
-- GCRA, one key per bucket. Time comes from Redis, so nodes never disagree.
-- KEYS[1]  bucket key
-- ARGV[1]  emission interval T in microseconds (period_us / requests_per_unit)
-- ARGV[2]  burst B (requests admissible at once from idle)
-- ARGV[3]  cost (hits for this request; normally 1)
-- Returns  {allowed (0|1), remaining, retry_after_us, reset_after_us}
local t = redis.call('TIME')
local now = tonumber(t[1]) * 1000000 + tonumber(t[2])
local T = tonumber(ARGV[1])
local B = tonumber(ARGV[2])
local cost = tonumber(ARGV[3])

local tat = tonumber(redis.call('GET', KEYS[1]))
if tat == nil or tat < now then tat = now end

local new_tat = tat + T * cost
local horizon = now + T * B              -- the latest TAT we accept

if new_tat > horizon then
  return {0, 0, math.ceil(new_tat - horizon), math.ceil(tat - now)}
end

-- string.format('%.0f') keeps every digit; plain number-to-string conversion
-- in Redis Lua uses 14 significant digits and would corrupt microsecond values.
local ttl_ms = math.ceil((new_tat - now) / 1000)
redis.call('SET', KEYS[1], string.format('%.0f', new_tat), 'PX', ttl_ms)
return {1, math.floor((horizon - new_tat) / T), 0, math.ceil(new_tat - now)}
