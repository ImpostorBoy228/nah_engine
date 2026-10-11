use std::env;
use std::error::Error;
use std::ffi::OsStr;
use std::fs;
use std::path::{Path, PathBuf};
use std::process::{Command, Stdio};
use std::time::SystemTime;

const RED: &str = "\x1b[31m";
const GREEN: &str = "\x1b[32m";
const RESET: &str = "\x1b[0m";

const RGFW_URL: &str = "https://github.com/ColleagueRiley/RGFW/releases/download/1.8.1/RGFW.h";

// every wayland protocol RGFW.h includes, relative to the wayland-protocols dir
const PROTOCOLS: &[(&str, &[&str])] = &[
    ("xdg-shell", &["stable/xdg-shell/xdg-shell.xml"]),
    (
        "xdg-toplevel-icon-v1",
        &["staging/xdg-toplevel-icon/xdg-toplevel-icon-v1.xml"],
    ),
    (
        "xdg-decoration-unstable-v1",
        &["unstable/xdg-decoration/xdg-decoration-unstable-v1.xml"],
    ),
    (
        "relative-pointer-unstable-v1",
        &["unstable/relative-pointer/relative-pointer-unstable-v1.xml"],
    ),
    (
        "pointer-constraints-unstable-v1",
        &["unstable/pointer-constraints/pointer-constraints-unstable-v1.xml"],
    ),
    (
        "xdg-output-unstable-v1",
        &["unstable/xdg-output/xdg-output-unstable-v1.xml"],
    ),
];

// git submodules living in the external dir, they are fetched with git, not curl
const SUBMODULES: &[&str] = &["luajit"];

const C_UNITS: &[&str] = &["window/rgfw.c"];
const CPP_UNITS: &[&str] = &["main.cpp", "window/rgfw_shit.cpp"];

// build flags, edit them here and nowhere else
const C_FLAGS: &[&str] = &[
    "-std=c23",
    "-Wall",
    "-Wextra",
    "-Wno-unused-result",
    "-O2",
    "-g",
];

const CXX_FLAGS: &[&str] = &[
    "-std=c++23",
    "-Wall",
    "-Wextra",
    "-Wno-unused-result",
    "-O2",
    "-g",
];

const LUAJIT_FLAGS: &[&str] = &["BUILDMODE=static", "CC=gcc -fPIC"];

const LIBS: &[&str] = &[
    "vulkan",
    "wayland-client",
    "wayland-cursor",
    "xkbcommon",
    "dl",
    "pthread",
    "m",
];

fn project_root() -> Result<PathBuf, Box<dyn Error>> {
    let cwd = env::current_dir()?;
    if cwd.join("Makefile").is_file() && cwd.join("src").is_dir() {
        return Ok(cwd);
    }
    if let Some(parent) = cwd.parent() {
        if parent.join("Makefile").is_file() && parent.join("src").is_dir() {
            return Ok(parent.to_path_buf());
        }
    }
    if let Ok(exe) = env::current_exe() {
        let mut p = exe.as_path().parent();
        while let Some(dir) = p {
            if dir.join("Makefile").is_file() && dir.join("src").is_dir() {
                return Ok(dir.to_path_buf());
            }
            p = dir.parent();
        }
    }
    let out = Command::new("git")
        .args(["rev-parse", "--show-toplevel"])
        .output()?;
    if out.status.success() {
        let s = String::from_utf8_lossy(&out.stdout).trim().to_string();
        if !s.is_empty() {
            return Ok(PathBuf::from(s));
        }
    }
    Ok(cwd.parent().unwrap_or(&cwd).to_path_buf())
}

struct Paths {
    root: PathBuf,
}

impl Paths {
    fn new() -> Result<Self, Box<dyn Error>> {
        Ok(Self {
            root: project_root()?,
        })
    }
    fn external(&self) -> PathBuf {
        self.root.join("shit/external")
    }
    fn build(&self) -> PathBuf {
        self.root.join("shit/build")
    }
    fn game(&self) -> PathBuf {
        self.build().join("game")
    }
    fn obj(&self) -> PathBuf {
        self.build().join("obj")
    }
    fn binshaders(&self) -> PathBuf {
        self.build().join("binshaders")
    }
    fn shaders(&self) -> PathBuf {
        self.root.join("shaders")
    }
    fn luascripts(&self) -> PathBuf {
        self.root.join("src/luascripts")
    }
    fn assets(&self) -> PathBuf {
        self.root.join("assets")
    }
    fn rgfw_header(&self) -> PathBuf {
        self.external().join("RGFW.h")
    }
    fn submodule(&self, name: &str) -> PathBuf {
        self.external().join(name)
    }
    fn luajit_src(&self) -> PathBuf {
        self.submodule("luajit").join("src")
    }
    fn luajit_lib(&self) -> PathBuf {
        self.luajit_src().join("libluajit.a")
    }
    fn protocols(&self) -> PathBuf {
        self.build().join("protocols")
    }
    // -I flags, every dir a source file may include from
    fn includes(&self) -> Vec<String> {
        [
            self.root.join("src"),
            self.external(),
            self.luajit_src(),
            self.protocols(),
            self.binshaders(),
        ]
        .iter()
        .map(|dir| format!("-I{}", dir.display()))
        .collect()
    }
    // path relative to the root, keeps progress labels short like cmake
    fn rel(&self, path: &Path) -> String {
        path.strip_prefix(&self.root)
            .unwrap_or(path)
            .display()
            .to_string()
    }
}

fn jobs() -> usize {
    std::thread::available_parallelism().map_or(1, |n| n.get())
}

// NO_COLOR, same convention as cmake
fn paint(color: &str, text: &str) -> String {
    if env::var_os("NO_COLOR").is_some() {
        return text.to_string();
    }
    format!("{color}{text}{RESET}")
}

fn push_arg(line: &mut String, arg: &OsStr) {
    let s = arg.to_string_lossy();
    if s.is_empty() || s.contains(char::is_whitespace) {
        line.push('"');
        line.push_str(&s);
        line.push('"');
    } else {
        line.push_str(&s);
    }
    line.push(' ');
}

// full copy-pasteable command line, the Debug repr of Command is not
fn cmd_line(cmd: &Command) -> String {
    let mut line = String::new();
    push_arg(&mut line, cmd.get_program());
    for arg in cmd.get_args() {
        push_arg(&mut line, arg);
    }
    line.trim_end().to_string()
}

// cmake style [done/total] counter, red counter, green label
struct Progress {
    done: usize,
    total: usize,
}

impl Progress {
    fn new(total: usize) -> Self {
        Self { done: 0, total }
    }
    fn step(&mut self, what: &str) {
        self.done += 1;
        let counter = paint(RED, &format!("[{}/{}]", self.done, self.total));
        println!("{} {}", counter, paint(GREEN, what));
    }
}

fn run(cmd: &mut Command) -> Result<(), Box<dyn Error>> {
    eprintln!("+ {}", cmd_line(cmd));
    let status = cmd.status()?;
    if !status.success() {
        return Err(format!("command failed: {}", cmd_line(cmd)).into());
    }
    Ok(())
}

// stdout of compilers is noise, warnings still reach stderr
fn run_muted(cmd: &mut Command) -> Result<(), Box<dyn Error>> {
    let line = cmd_line(cmd);
    let status = cmd.stdout(Stdio::null()).status()?;
    if !status.success() {
        return Err(format!("command failed: {line}").into());
    }
    Ok(())
}

// mtime of a file, None if missing, so a missing output always counts as stale
fn mtime(path: &Path) -> Option<SystemTime> {
    fs::metadata(path).and_then(|m| m.modified()).ok()
}

// is src newer than dst. a missing dst is always stale
fn is_newer(src: &Path, dst: &Path) -> bool {
    match (mtime(src), mtime(dst)) {
        (Some(s), Some(d)) => s > d,
        (Some(_), None) => true,
        _ => false,
    }
}

// prerequisites (sources + headers) listed in a gcc -MMD depfile, paths only
fn dep_prereqs(dep: &Path) -> Vec<PathBuf> {
    let Ok(text) = fs::read_to_string(dep) else {
        return Vec::new();
    };
    // join backslash-newline line continuations into a single space
    let joined = text.replace("\\\r\n", " ").replace("\\\n", " ");
    let mut out = Vec::new();
    let mut cur = String::new();
    let mut chars = joined.chars().peekable();
    let mut seen_target = false;
    while let Some(c) = chars.next() {
        match c {
            '\\' => {
                // escaped char (e.g. a space in a path), take the next one literally
                if let Some(&n) = chars.peek() {
                    cur.push(n);
                    chars.next();
                }
            }
            ' ' | '\t' | '\n' | '\r' => {
                if !cur.is_empty() {
                    // first token is the target, -MP phony targets end with ':'
                    if seen_target && !cur.ends_with(':') {
                        out.push(PathBuf::from(&cur));
                    }
                    seen_target = true;
                    cur.clear();
                }
            }
            _ => cur.push(c),
        }
    }
    if !cur.is_empty() && seen_target && !cur.ends_with(':') {
        out.push(PathBuf::from(&cur));
    }
    out
}

// is the object stale: missing, older than its source, or older than any header.
// a missing depfile counts as stale so the header deps get captured on the first run
fn obj_stale(obj: &Path, dep: &Path, source: &Path) -> bool {
    if mtime(obj).is_none() || mtime(dep).is_none() {
        return true;
    }
    if is_newer(source, obj) {
        return true;
    }
    dep_prereqs(dep).iter().any(|pre| is_newer(pre, obj))
}

fn fetch_submodules(p: &Paths) -> Result<(), Box<dyn Error>> {
    let fresh = SUBMODULES
        .iter()
        .any(|name| !p.submodule(name).join(".git").exists());

    // NOTE: `git submodule --init` is invalid (init is a subcommand of update).
    let mut cmd = Command::new("git");
    cmd.arg("submodule").arg("update");
    if fresh {
        cmd.arg("--init").arg("--recursive");
    } else {
        cmd.arg("--remote").arg("--merge");
    }
    cmd.current_dir(&p.root);
    run(&mut cmd)
}

fn depmake(p: &Paths) -> Result<(), Box<dyn Error>> {
    let src = p.luajit_src();
    if !src.is_dir() {
        return Err(format!("luajit submodule missing at {}", src.display()).into());
    }
    let lib = p.luajit_lib();

    // BUILDMODE=static builds only libluajit.a
    let mut cmd = Command::new("make");
    cmd.arg("-C")
        .arg(&src)
        .arg(format!("-j{}", jobs()))
        .args(LUAJIT_FLAGS);
    run(&mut cmd)?;

    if !lib.is_file() {
        return Err(format!("luajit build produced no {}", lib.display()).into());
    }
    println!("depmake: {} -> {}", src.display(), lib.display());
    Ok(())
}

fn depfetch(p: &Paths) -> Result<(), Box<dyn Error>> {
    fs::create_dir_all(p.external())?;
    fs::create_dir_all(p.game())?;
    fs::create_dir_all(p.obj())?;
    fs::create_dir_all(p.binshaders())?;

    let rgfw = p.rgfw_header();
    if !rgfw.is_file() {
        run(Command::new("curl")
            .arg(RGFW_URL)
            .arg("-L")
            .arg("-o")
            .arg(&rgfw))?;
    } else {
        println!("RGFW.h already present");
    }

    println!("project root: {}", p.root.display());
    fetch_submodules(p)?;
    println!("depfetch: ok");
    Ok(())
}

fn wayland_protocols_root() -> Result<PathBuf, Box<dyn Error>> {
    let out = Command::new("pkg-config")
        .args(["--variable=pkgdatadir", "wayland-protocols"])
        .output()?;

    if out.status.success() {
        let dir = PathBuf::from(String::from_utf8_lossy(&out.stdout).trim().to_string());
        if dir.is_dir() {
            return Ok(dir);
        }
    }

    let fallback = PathBuf::from("/usr/share/wayland-protocols");
    if fallback.is_dir() {
        return Ok(fallback);
    }

    Err("wayland-protocols not found (install libwayland-dev + wayland-protocols)".into())
}

// glsl sources living in shaders/, sorted so the build order is stable
fn shader_sources(p: &Paths) -> Result<Vec<PathBuf>, Box<dyn Error>> {
    let dir = p.shaders();
    if !dir.is_dir() {
        return Ok(Vec::new());
    }
    let mut sources: Vec<PathBuf> = Vec::new();
    for entry in fs::read_dir(&dir)? {
        let path = entry?.path();
        if matches!(
            path.extension().and_then(|s| s.to_str()),
            Some("vert") | Some("frag") | Some("comp")
        ) {
            sources.push(path);
        }
    }
    sources.sort();
    Ok(sources)
}

// xml of every protocol we need, relative to the wayland-protocols dir
fn protocol_xmls() -> Result<Vec<(&'static str, PathBuf)>, Box<dyn Error>> {
    let root = wayland_protocols_root()?;
    let mut found = Vec::new();
    for (name, candidates) in PROTOCOLS {
        let mut xml = None;
        for rel in *candidates {
            let path = root.join(rel);
            if path.is_file() {
                xml = Some(path);
                break;
            }
        }
        let xml = match xml {
            Some(path) => path,
            None => {
                return Err(
                    format!("wayland protocol {name} not found in {}", root.display()).into(),
                );
            }
        };
        found.push((*name, xml));
    }
    Ok(found)
}

fn scan_protocols(
    p: &Paths,
    protos: &[(&'static str, PathBuf)],
    prog: &mut Progress,
) -> Result<Vec<PathBuf>, Box<dyn Error>> {
    let out = p.protocols();
    fs::create_dir_all(&out)?;
    let mut codes = Vec::new();

    for (name, xml) in protos {
        let header = out.join(format!("{name}.h"));
        let code = out.join(format!("{name}-protocol.c"));

        // regenerate only when the protocol xml changed
        if is_newer(xml, &header) || mtime(&header).is_none() {
            prog.step(&format!("Generating {}", p.rel(&header)));
            let mut head = Command::new("wayland-scanner");
            head.arg("client-header").arg(xml).arg(&header);
            println!("{}", cmd_line(&head));
            run_muted(&mut head)?;
        } else {
            prog.step(&format!("Up-to-date {}", p.rel(&header)));
        }

        if is_newer(xml, &code) || mtime(&code).is_none() {
            prog.step(&format!("Generating {}", p.rel(&code)));
            let mut body = Command::new("wayland-scanner");
            body.arg("private-code").arg(xml).arg(&code);
            println!("{}", cmd_line(&body));
            run_muted(&mut body)?;
        } else {
            prog.step(&format!("Up-to-date {}", p.rel(&code)));
        }

        codes.push(code);
    }

    Ok(codes)
}

fn shadermake(p: &Paths, sources: &[PathBuf], prog: &mut Progress) -> Result<(), Box<dyn Error>> {
    let out = p.binshaders();

    if sources.is_empty() {
        println!("shadermake: no shader sources in {}", p.shaders().display());
        return Ok(());
    }
    fs::create_dir_all(&out)?;

    for src in sources {
        let stage = match src.extension().and_then(|s| s.to_str()) {
            Some("vert") => "vert",
            Some("frag") => "frag",
            _ => "comp",
        };
        let file_stem = src.file_stem().unwrap().to_string_lossy().to_string();
        // stage in the stem: prim.vert and prim.frag would both be prim.bin.h otherwise
        let stem = format!("{file_stem}_{stage}");
        let spv = out.join(format!("{stem}.spv"));
        let header = out.join(format!("{stem}.bin.h"));

        // recompile only when the glsl source changed; regenerate the header only
        // when the spv did (glslc output is byte-stable, so mtime is enough)
        if is_newer(src, &spv) || mtime(&spv).is_none() {
            prog.step(&format!("Building shader object {}", p.rel(&spv)));
            let mut cmd = Command::new("glslc");
            cmd.arg(format!("-fshader-stage={stage}"))
                .arg(src)
                .arg("-o")
                .arg(&spv);
            println!("{}", cmd_line(&cmd));
            run_muted(&mut cmd)?;
        } else {
            prog.step(&format!("Up-to-date shader object {}", p.rel(&spv)));
        }

        if !is_newer(&spv, &header) && mtime(&header).is_some() {
            continue;
        }

        let bytes = fs::read(&spv)?;
        let mut h = String::new();
        h.push_str(&format!("// generated from {stem}\n"));
        h.push_str("#pragma once\n#include <stdint.h>\n");
        h.push_str(&format!(
            "static const uint32_t {}_size = {};\n",
            stem,
            bytes.len() / 4
        ));
        h.push_str(&format!(
            "static const uint8_t {}_data[{}] = {{\n",
            stem,
            bytes.len()
        ));
        for chunk in bytes.chunks(16) {
            let line: Vec<String> = chunk.iter().map(|b| format!("0x{b:02x},")).collect();
            h.push_str(&line.join(""));
            h.push('\n');
        }
        h.push_str("};\n");
        fs::write(&header, h)?;
    }
    Ok(())
}

fn copy_tree(src: &Path, dst: &Path) -> Result<(), Box<dyn Error>> {
    fs::create_dir_all(dst)?;
    for entry in fs::read_dir(src)? {
        let entry = entry?;
        let to = dst.join(entry.file_name());
        if entry.path().is_dir() {
            copy_tree(&entry.path(), &to)?;
        } else if is_newer(&entry.path(), &to) {
            // skip the copy when the destination is already up to date
            fs::copy(entry.path(), &to)?;
        }
    }
    Ok(())
}

// BLACKBOX
fn deploy_luascripts(p: &Paths) {
    // BLACKBOX
    let src = p.luascripts();
    let dst = p.game();
    if !src.is_dir() {
        println!("deploy: no {} to copy", src.display());
        return;
    }
    if let Err(e) = copy_tree(&src, &dst) {
        println!("deploy: {} failed: {e}", src.display());
        return;
    }
    println!("deploy: {} -> {}", src.display(), dst.display());
}

// BLACKBOX
fn deploy_assets(p: &Paths) {
    // BLACKBOX
    let src = p.assets();
    let dst = p.game().join("assets");
    if !src.is_dir() {
        println!("deploy: no {} to copy", src.display());
        return;
    }
    if let Err(e) = copy_tree(&src, &dst) {
        println!("deploy: {} failed: {e}", src.display());
        return;
    }
    println!("deploy: {} -> {}", src.display(), dst.display());
}

// json string escaping, paths only but quotes happen
fn json_str(s: &str) -> String {
    s.replace('\\', "\\\\").replace('"', "\\\"")
}

// headers clangd must be able to open on their own, they are not compiled
fn src_headers(p: &Paths) -> Vec<PathBuf> {
    let mut out = Vec::new();
    let mut stack = vec![p.root.join("src")];
    while let Some(dir) = stack.pop() {
        let entries = match fs::read_dir(&dir) {
            Ok(entries) => entries,
            Err(_) => continue,
        };
        for entry in entries.flatten() {
            let path = entry.path();
            if path.is_dir() {
                stack.push(path);
                continue;
            }
            match path.extension().and_then(|s| s.to_str()) {
                Some("h") | Some("hpp") => out.push(path),
                _ => {}
            }
        }
    }
    out.sort();
    out
}

// compile_commands.json, clangd and every other editor reads it from the root.
// paths are absolute so nothing depends on the reader cwd.
fn compdb(p: &Paths, units: &[(PathBuf, bool)]) -> Result<(), Box<dyn Error>> {
    let mut out = String::from("[\n");
    for (i, (unit, is_cxx)) in units.iter().enumerate() {
        let mut args: Vec<String> = vec![if *is_cxx { "g++" } else { "gcc" }.to_string()];
        args.extend(compile_flags(p, *is_cxx));
        args.push(unit.display().to_string());

        let mut entry = format!("  {{\n    \"directory\": \"{}\",\n", json_str(&p.root.display().to_string()));
        entry.push_str(&format!("    \"file\": \"{}\",\n", json_str(&unit.display().to_string())));
        entry.push_str("    \"arguments\": [\n");
        let args: Vec<String> = args
            .iter()
            .map(|arg| format!("      \"{}\"", json_str(arg)))
            .collect();
        entry.push_str(&args.join(",\n"));
        entry.push_str("\n    ]\n  }");
        out.push_str(&entry);
        if i + 1 < units.len() {
            out.push(',');
        }
        out.push('\n');
    }
    out.push_str("]\n");
    fs::write(p.root.join("compile_commands.json"), out)?;
    Ok(())
}

// every include flag the real build uses, one source of truth for the compilers
fn compile_flags(p: &Paths, is_cxx: bool) -> Vec<String> {
    let mut flags: Vec<String> = if is_cxx { CXX_FLAGS } else { C_FLAGS }
        .iter()
        .map(|flag| flag.to_string())
        .collect();
    flags.extend(p.includes());
    flags
}

fn compile(
    p: &Paths,
    prog: &mut Progress,
    compiler: &str,
    unit: &PathBuf,
) -> Result<(PathBuf, bool), Box<dyn Error>> {
    let name = unit
        .file_name()
        .map(|s| s.to_string_lossy().to_string())
        .unwrap_or_default();
    let stem = name.split('.').next().unwrap_or("unit").to_string();
    let obj = p.obj().join(format!("{stem}.o"));
    let dep = p.obj().join(format!("{stem}.d"));
    let is_cxx = compiler.contains("++");
    let kind = if is_cxx { "CXX" } else { "C" };
    let flags = compile_flags(p, is_cxx);

    // skip the compile if the object is newer than the source and every header
    if !obj_stale(&obj, &dep, unit) {
        prog.step(&format!("Up-to-date {kind} object {}", p.rel(&obj)));
        return Ok((obj, false));
    }

    prog.step(&format!("Building {kind} object {}", p.rel(&obj)));

    let mut cmd = Command::new(compiler);
    // -MMD -MP emit a sidecar depfile (sources + headers) for the staleness check,
    // -MP adds phony targets so a removed header does not break the next build
    cmd.args(&flags)
        .arg("-MMD")
        .arg("-MP")
        .arg("-MF")
        .arg(&dep)
        .arg("-c")
        .arg(unit)
        .arg("-o")
        .arg(&obj);
    println!("{}", cmd_line(&cmd));
    run_muted(&mut cmd)?;

    Ok((obj, true))
}

fn make(p: &Paths) -> Result<(), Box<dyn Error>> {
    let sources = shader_sources(p)?;
    let protos = protocol_xmls()?;

    // shaders + 2 wayland-scanner runs per protocol + protocol .c + units + link
    let mut prog = Progress::new(
        sources.len() + protos.len() * 2 + protos.len() + C_UNITS.len() + CPP_UNITS.len() + 1,
    );

    shadermake(p, &sources, &mut prog)?;
    let codes = scan_protocols(p, &protos, &mut prog)?;

    let luajit = p.luajit_lib();
    if !luajit.is_file() {
        return Err(format!("{} missing, run depmake", luajit.display()).into());
    }

    fs::create_dir_all(p.obj())?;
    fs::create_dir_all(p.game())?;

    let mut units: Vec<(PathBuf, bool)> = Vec::new();
    for unit in C_UNITS {
        units.push((p.root.join("src").join(unit), false));
    }
    for unit in CPP_UNITS {
        units.push((p.root.join("src").join(unit), true));
    }
    for header in src_headers(p) {
        let is_cxx = header.extension().and_then(|s| s.to_str()) == Some("hpp");
        units.push((header, is_cxx));
    }
    compdb(p, &units)?;

    let mut objs: Vec<PathBuf> = Vec::new();
    let mut any_rebuilt = false;

    for unit in C_UNITS {
        let (obj, rebuilt) = compile(p, &mut prog, "gcc", &p.root.join("src").join(unit))?;
        any_rebuilt |= rebuilt;
        objs.push(obj);
    }

    for code in &codes {
        let (obj, rebuilt) = compile(p, &mut prog, "gcc", code)?;
        any_rebuilt |= rebuilt;
        objs.push(obj);
    }

    for unit in CPP_UNITS {
        let (obj, rebuilt) = compile(p, &mut prog, "g++", &p.root.join("src").join(unit))?;
        any_rebuilt |= rebuilt;
        objs.push(obj);
    }

    let exe = p.game().join("nah_engine");

    // link only if something changed or the exe is missing/older than the libs
    let need_link = any_rebuilt
        || mtime(&exe).is_none()
        || is_newer(&luajit, &exe)
        || objs.iter().any(|o| is_newer(o, &exe));
    if !need_link {
        prog.step(&format!("Up-to-date executable {}", p.rel(&exe)));
        println!("make: nothing to do -> {}", exe.display());
        deploy_luascripts(p);
        deploy_assets(p);
        return Ok(());
    }

    prog.step(&format!("Linking CXX executable {}", p.rel(&exe)));

    let mut link = Command::new("g++");
    // whole-archive: lua_register pulls the C modules in by name, the linker
    // cannot see those references on its own.
    link.arg("-Wl,--whole-archive")
        .arg(&luajit)
        .arg("-Wl,--no-whole-archive");
    // export-dynamic: ffi.C resolves symbols via dlsym on this process, the
    // C functions (gachicreate etc) must live in the dynamic symbol table.
    link.arg("-rdynamic");
    for obj in &objs {
        link.arg(obj);
    }
    for lib in LIBS {
        link.arg(format!("-l{lib}"));
    }
    link.arg("-o").arg(&exe);
    println!("{}", cmd_line(&link));
    run_muted(&mut link)?;

    deploy_luascripts(p);
    deploy_assets(p);

    println!("make: done -> {}", exe.display());
    Ok(())
}

fn main() -> Result<(), Box<dyn Error>> {
    let args: Vec<String> = env::args().collect();
    let p = Paths::new()?;

    if args.len() < 2 {
        depfetch(&p)?;
    } else {
        match args[1].as_str() {
            "make" => make(&p)?,
            "depmake" => depmake(&p)?,
            "depfetch" => depfetch(&p)?,
            "shadermake" => {
                let sources = shader_sources(&p)?;
                let mut prog = Progress::new(sources.len());
                shadermake(&p, &sources, &mut prog)?;
            }
            "protocols" => {
                let protos = protocol_xmls()?;
                let mut prog = Progress::new(protos.len() * 2);
                scan_protocols(&p, &protos, &mut prog)?;
            }
            "clean" => {
                if p.build().is_dir() {
                    fs::remove_dir_all(p.build())?;
                }
                println!("clean: removed {}", p.build().display());
            }
            other => {
                eprintln!("unknown command: {other}");
                eprintln!("usage: citrus [make|depmake|depfetch|shadermake|protocols|clean]");
                std::process::exit(2);
            }
        }
    }
    Ok(())
}
