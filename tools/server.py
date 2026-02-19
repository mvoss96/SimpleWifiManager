from http.server import BaseHTTPRequestHandler, HTTPServer
from time import sleep
import json
import urllib.parse
import os

MOCK_APS = [
    {"ssid": "HomeNet", "rssi": -40, "enc": 4},
    {"ssid": "IoT-Lab", "rssi": -55, "enc": 4},
    {"ssid": "Guest", "rssi": -70, "enc": 4},
    {"ssid": "open", "rssi": -75, "enc": 0},
]


class Handler(BaseHTTPRequestHandler):
    def do_GET(self):
        if self.path == "/" or self.path == "/index.html":
            self.send_response(200)
            self.send_header("Content-Type", "text/html; charset=utf-8")
            self.end_headers()
            with open("index.html", "rb") as f:
                self.wfile.write(f.read())
            return

        if self.path == "/scan":
            # Return compact arrays: [[ssid,rssi,enc],...]
            compact = [[ap["ssid"], ap["rssi"], ap["enc"]] for ap in MOCK_APS]
            payload = json.dumps(compact).encode("utf-8")
            sleep(1)  # Simulate scanning delay
            self.send_response(200)
            self.send_header("Content-Type", "application/json")
            self.send_header("Cache-Control", "no-store")
            self.end_headers()
            self.wfile.write(payload)
            return

        self.send_response(404)
        self.send_header("Content-Type", "text/plain; charset=utf-8")
        self.end_headers()
        self.wfile.write(b"Not found")

    def do_POST(self):
        # Handle form submission from captive portal (POST /)
        if self.path != "/":
            self.send_response(404)
            self.send_header("Content-Type", "text/plain; charset=utf-8")
            self.end_headers()
            self.wfile.write(b"Not found")
            return

        content_length = int(self.headers.get('Content-Length', 0))
        body = self.rfile.read(content_length).decode('utf-8')
        params = urllib.parse.parse_qs(body)
        ssid = params.get('ssid', [''])[0]
        password = params.get('password', [''])[0]

        print(f"Received WiFi credentials: SSID='{ssid}', Password='{password}'")
        # self.send_response(200)
        # self.send_header("Content-Type", "text/html; charset=utf-8")
        # self.end_headers()
        # with open("saved.html", "rb") as f:
        #     self.wfile.write(f.read())
        return

def main():
    server = HTTPServer(("127.0.0.1", 8000), Handler)
    print("Serving on http://127.0.0.1:8000")
    server.serve_forever()


if __name__ == "__main__":
    main()
