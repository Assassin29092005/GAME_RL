#!/usr/bin/env python3
"""HellwalkerRL - serve web/ locally with caching off (stdlib only): python web/dev/serve.py [--port 8000] [--render-headers]

The same as `python -m http.server` run from web/, plus `Cache-Control: no-cache` so an edited ES module is never
served stale from the browser's cache. --render-headers also sends the response headers render.yaml gives the
deployed site (the Content-Security-Policy among them), to check locally that nothing on the page breaks them.
Then open http://127.0.0.1:8000/ (demo data) or http://127.0.0.1:8000/?mock=http://127.0.0.1:8099 (the mock)."""

import argparse
import functools
import os
import re
from http.server import SimpleHTTPRequestHandler, ThreadingHTTPServer

WEB = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
RENDER_YAML = os.path.join(os.path.dirname(WEB), "render.yaml")


def render_headers():
	"""[(path glob, name, value)] from render.yaml's `headers:` list (the simple layout that file uses)."""
	with open(RENDER_YAML, "r", encoding="utf-8") as f:
		text = f.read()
	out = []
	for m in re.finditer(r"-\s*path:\s*(\S+)\s*\n\s*name:\s*(.+?)\s*\n\s*value:\s*(.+?)\s*\n", text):
		value = m.group(3)
		if len(value) >= 2 and value[0] == value[-1] and value[0] in "\"'":
			value = value[1:-1]
		out.append((m.group(1), m.group(2), value))
	return out


class NoCache(SimpleHTTPRequestHandler):
	extensions_map = {**SimpleHTTPRequestHandler.extensions_map, ".js": "text/javascript", ".mjs": "text/javascript", ".svg": "image/svg+xml"}
	extra = []

	def end_headers(self):
		sent = set()
		for glob, name, value in self.extra:
			prefix = glob.rstrip("*")
			if self.path.startswith(prefix) and name.lower() not in sent:
				self.send_header(name, value)
				sent.add(name.lower())
		if "cache-control" not in sent:
			self.send_header("Cache-Control", "no-cache")
		super().end_headers()


def main():
	ap = argparse.ArgumentParser(description=__doc__.split("\n")[0])
	ap.add_argument("--host", default="127.0.0.1")
	ap.add_argument("--port", type=int, default=8000)
	ap.add_argument("--render-headers", action="store_true", help="send render.yaml's headers (CSP, ...) too")
	args = ap.parse_args()
	if args.render_headers:
		NoCache.extra = render_headers()
		for glob, name, _ in NoCache.extra:
			print("  header %-28s on %s" % (name, glob))
	srv = ThreadingHTTPServer((args.host, args.port), functools.partial(NoCache, directory=WEB))
	print("serving %s on http://%s:%d/" % (WEB, args.host, args.port), flush=True)
	try:
		srv.serve_forever()
	except KeyboardInterrupt:
		pass


if __name__ == "__main__":
	main()
