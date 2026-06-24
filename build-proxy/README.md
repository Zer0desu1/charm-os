# Vecta build proxy (Cloudflare Worker)

Hides the GitHub token from the app. The app calls this Worker; the Worker
triggers the GitHub Actions firmware build with the server-side secret token.
Customers never see or enter a token.

## One-time deploy

1. Install Wrangler and log in (opens a browser once):
   ```
   npm install -g wrangler
   wrangler login
   ```

2. From this folder, set the secret token (a fine-grained GitHub PAT scoped to
   the `charm-os` repo with **Actions: Read and write**, **Contents: Read-only**):
   ```
   wrangler secret put GH_TOKEN
   ```
   (Optional: gate the endpoint so only your app can call it:
   `wrangler secret put APP_KEY` — then put the same value in `BUILD_PROXY_KEY`
   in `AmoledSenderExpo/lib/device.ts`.)

3. Deploy:
   ```
   wrangler deploy
   ```
   Wrangler prints a URL like `https://vecta-build-proxy.<your-subdomain>.workers.dev`.

4. Put that URL in `AmoledSenderExpo/lib/device.ts` → `BUILD_PROXY_URL`, rebuild
   the app. Done — the Store now builds modules via your backend, no token in the
   app.

## Test
```
curl https://vecta-build-proxy.<your-subdomain>.workers.dev/ping            # -> ok
curl -X POST https://.../build -H "Content-Type: application/json" \
     -d '{"mods":["clock","game"]}'                                          # -> {"ok":true,...}
```
After a successful build the asset appears at:
`https://github.com/Zer0desu1/charm-os/releases/download/custom/charm-clock-game.bin`

## Free tier
Cloudflare Workers free plan = 100k requests/day — far more than enough; each
module install is a couple of requests.
