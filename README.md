# DepAnalyzer

Source code dependency analyzer. Scans a directory and outputs JSON — internal deps, external deps, cross-file calls. **55 languages, zero dependencies.**

## Build

```bash
make
```

**Windows (MSYS2):**
```bash
make CC=gcc CFLAGS="-Wall -std=c99 -O2 -D_FORTIFY_SOURCE=0 -Isrc/core -Isrc/parsers"
```

## Usage

```bash
# Analyze a local directory
depanalyzer ./my_project

# Save to file
depanalyzer ./my_project --output deps.json

# With stats report
depanalyzer ./my_project --output deps.json --stats
```

## Analyze a GitHub repo (no clone needed)

```bash
# Requires Python 3.8+
python depanalyzer-github user/repo
python depanalyzer-github user/repo --output deps.json
python depanalyzer-github user/repo --lang c python shell
python depanalyzer-github user/repo --lang rust --max-files 5000 --stats

# With GitHub token (5000 req/h instead of 60)
GITHUB_TOKEN=ghp_xxxx python depanalyzer-github user/repo
```

**`--lang` values:**
`c` `cpp` `python` `javascript` `typescript` `java` `kotlin` `swift` `go` `rust` `ruby` `php` `lua` `dart` `r` `julia` `zig` `crystal` `nim` `v` `perl` `ocaml` `fsharp` `scala` `haskell` `elixir` `erlang` `clojure` `csharp` `groovy` `powershell` `html` `css` `vue` `svelte` `json` `toml` `yaml` `sql` `graphql` `proto` `terraform` `nix` `solidity` `glsl` `shell` `assembly` `cmake` `bazel` `makefile` `dockerfile` `cobol` `fortran` `markdown`

## Visualizer

Open `depanalyzer-map.html` in a browser → drag and drop your `deps.json`.

Force-directed graph, canvas rendering, optimized for 1000+ nodes.

## Output

```json
{
  "project": "./my_project",
  "file_count": 42,
  "files": [
    {
      "name": "main.py",
      "path": "/path/to/main.py",
      "language": "python",
      "internal_dependencies": ["utils.py", "models.py"],
      "external_dependencies": ["requests", "flask"],
      "internal_calls": ["helpers.py"]
    }
  ]
}
```

## License

MIT
