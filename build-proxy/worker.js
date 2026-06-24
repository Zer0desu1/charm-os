// ============================================================================
// Vecta build proxy (Cloudflare Worker)
//
//   Hides the GitHub token so the PHONE APP never sees it. The app POSTs the
//   wanted module set here; this Worker triggers the GitHub Actions build
//   (workflow_dispatch) using the server-held secret token. The built .bin is
//   published to the public "custom" release, which the app downloads directly
//   (no auth needed - the repo/release is public).
//
//   Endpoints:
//     GET  /ping                 -> "ok"           (app uses this to verify)
//     POST /build {mods:[...]}    -> {ok:true}      (triggers the cloud build)
//
//   Secrets / vars (set with wrangler):
//     GH_TOKEN   (secret) fine-grained PAT: repo charm-os, Actions R+W, Contents R
//     GH_OWNER, GH_REPO, GH_WORKFLOW   (vars, see wrangler.toml)
//     APP_KEY    (optional secret) if set, requests must send  X-App-Key: <key>
// ============================================================================
export default {
  async fetch(req, env) {
    const url = new URL(req.url);
    const cors = {
      'Access-Control-Allow-Origin': '*',
      'Access-Control-Allow-Methods': 'GET,POST,OPTIONS',
      'Access-Control-Allow-Headers': 'Content-Type,X-App-Key',
    };
    if (req.method === 'OPTIONS') return new Response(null, { headers: cors });

    if (url.pathname === '/ping') {
      return new Response('ok', { headers: cors });
    }

    if (req.method === 'POST' && url.pathname === '/build') {
      if (env.APP_KEY && req.headers.get('x-app-key') !== env.APP_KEY) {
        return new Response('forbidden', { status: 403, headers: cors });
      }
      let body = {};
      try { body = await req.json(); } catch {}
      const mods = Array.isArray(body.mods) ? body.mods : [];
      // sanitize: lowercase alnum ids only
      const clean = mods
        .map(m => String(m).trim().toLowerCase())
        .filter(m => /^[a-z0-9_]+$/.test(m));

      const ghUrl = `https://api.github.com/repos/${env.GH_OWNER}/${env.GH_REPO}` +
                    `/actions/workflows/${env.GH_WORKFLOW}/dispatches`;
      const r = await fetch(ghUrl, {
        method: 'POST',
        headers: {
          Authorization: `Bearer ${env.GH_TOKEN}`,
          Accept: 'application/vnd.github+json',
          'X-GitHub-Api-Version': '2022-11-28',
          'User-Agent': 'vecta-build-proxy',   // GitHub API requires a UA
          'Content-Type': 'application/json',
        },
        body: JSON.stringify({ ref: 'main', inputs: { mods: clean.join(',') } }),
      });
      if (r.status !== 204) {
        const txt = await r.text().catch(() => '');
        return new Response(`dispatch ${r.status}: ${txt.slice(0, 200)}`,
                            { status: 502, headers: cors });
      }
      return new Response(JSON.stringify({ ok: true, mods: clean }),
                          { headers: { ...cors, 'Content-Type': 'application/json' } });
    }

    return new Response('not found', { status: 404, headers: cors });
  },
};
