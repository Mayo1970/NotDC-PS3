#!/usr/bin/env python3
# Builds the compatibility page from issues labelled both "compatibility" and "approved".
# Usage: build.py <out_dir> [issues.json]  (the JSON file replaces the API, for local tests)
import json
import os
import re
import shutil
import sys
import urllib.parse
import urllib.request

HERE = os.path.dirname(os.path.abspath(__file__))
TEMPLATE = os.path.join(HERE, "..", "ISSUE_TEMPLATE", "game_compatibility.yml")
LABELS = "compatibility,approved"
HIDDEN_REASONS = {"not_planned", "duplicate"}
STATUSES = ["Perfect", "Playable", "In-game", "Menus / Intro", "Boots", "Nothing"]
NAME, STATUS, DESCRIPTION = "Game title", "Compatibility status", "What happens?"


def form_labels():
    # Field labels only: checkbox options are "- label:" lines, which the regex skips.
    with open(TEMPLATE, encoding="utf-8") as f:
        return set(re.findall(r"^\s+label:\s*(.+?)\s*$", f.read(), re.M))


def fetch_issues(repo, token):
    issues, page = [], 1
    while True:
        query = urllib.parse.urlencode({"labels": LABELS, "state": "all", "per_page": 100, "page": page})
        req = urllib.request.Request(f"https://api.github.com/repos/{repo}/issues?{query}")
        req.add_header("Accept", "application/vnd.github+json")
        if token:
            req.add_header("Authorization", f"Bearer {token}")
        with urllib.request.urlopen(req) as r:
            batch = json.load(r)
        issues += batch
        if len(batch) < 100:
            return issues
        page += 1


def parse_form(body, headings):
    # Only known headings start a section, so a "###" line typed into a text field stays text.
    fields, current = {}, None
    for line in (body or "").replace("\r\n", "\n").split("\n"):
        m = re.match(r"^### (.+?)\s*$", line)
        if m and m.group(1) in headings and m.group(1) not in fields:
            current = m.group(1)
            fields[current] = []
        elif current:
            fields[current].append(line)
    out = {}
    for key, lines in fields.items():
        value = "\n".join(lines).strip()
        out[key] = "" if value == "_No response_" else value
    return out


def main():
    out_dir = sys.argv[1]
    if len(sys.argv) > 2:
        with open(sys.argv[2], encoding="utf-8") as f:
            issues = json.load(f)
    else:
        issues = fetch_issues(os.environ["GH_REPO"], os.environ.get("GH_TOKEN"))

    headings = form_labels()
    games = []
    for issue in issues:
        if "pull_request" in issue or issue.get("state_reason") in HIDDEN_REASONS:
            continue
        fields = parse_form(issue.get("body"), headings)
        name = fields.get(NAME, "")
        status = fields.get(STATUS, "").split(":", 1)[0].strip()
        if not name or status not in STATUSES:
            print(f"::warning::#{issue['number']} skipped: no game title or unknown status")
            continue
        games.append({"number": issue["number"], "name": name, "status": status,
                      "description": fields.get(DESCRIPTION, "")})

    games.sort(key=lambda g: (g["name"].casefold(), g["number"]))
    for g in games:
        del g["number"]

    os.makedirs(out_dir, exist_ok=True)
    shutil.copy(os.path.join(HERE, "index.html"), out_dir)
    with open(os.path.join(out_dir, "compat.json"), "w", encoding="utf-8") as f:
        json.dump({"repo": os.environ.get("GH_REPO", ""), "statuses": STATUSES, "games": games},
                  f, ensure_ascii=False, indent=1)
    print(f"{len(games)} games written")


if __name__ == "__main__":
    main()
