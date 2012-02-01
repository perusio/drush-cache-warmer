-- Perform the location multi capture to issue the requests in parallel.
-- This Lua code is part of the cache_warmer.

-- First we grab the POST data.
ngx.req.read_body() -- read the request body
local post_data = ngx.req.get_post_args() -- capture the arguments.
local base_uri = post_data['base_uri']

-- Release that entry from the POST data table.
post_data['base_uri'] = nil

local requests = {} -- requests table

-- Building the location for making the parallel requests.
function build_req_uri(base_uri, uri)
   return string.format('/parallel-reqs?u=%s/%s', base_uri, uri)
end -- build_req_uri

-- Loop over the post_data table (contains the URIs to be hit).
for i, u in pairs(post_data) do
   -- All requests are HEAD requests.
   table.insert(requests, { build_req_uri(base_uri, u), { method = ngx.HTTP_HEAD }})
end

-- Issue the requests and store the responses.
local responses = ngx.location_multi(requests)

-- Process the responses.
for i, r in pairs(responses) do
   ngx.say(i, r)
end
