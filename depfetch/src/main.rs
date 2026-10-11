use std::env;
use std::error::Error;
use std::fs;
use std::path::PathBuf;
use std::process::Command;

const RGFW_URL: &str = "https://github.com/ColleagueRiley/RGFW/releases/download/1.8.1/RGFW.h";

/* every wayland protocol RGFW.h includes, relative to the wayland-protocols dir */
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

const C_UNITS: &[&str] = &["window/rgfw.c"];
const CPP_UNITS: &[&str] = &["main.cpp", "window/rgfw_shit.cpp"];

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
    fn bin(&self) -> PathBuf {
        self.build().join("bin")
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
    fn rgfw_header(&self) -> PathBuf {
        self.external().join("RGFW.h")
    }
    fn protocols(&self) -> PathBuf {
        self.build().join("protocols")
    }
}

fn run(cmd: &mut Command) -> Result<(), Box<dyn Error>> {
    eprintln!("+ {:?}", cmd);
    let status = cmd.status()?;
    if !status.success() {
        return Err(format!("command failed: {:?}", cmd).into());
    }
    Ok(())
}

fn depfetch(p: &Paths) -> Result<(), Box<dyn Error>> {
    fs::create_dir_all(p.external())?;
    fs::create_dir_all(p.bin())?;
    fs::create_dir_all(p.obj())?;
    fs::create_dir_all(p.binshaders())?;

    let rgfw = p.rgfw_header();
    if !rgfw.is_file() {
        run(Command::new("curl").arg(RGFW_URL).arg("-L").arg("-o").arg(&rgfw))?;
    } else {
        println!("RGFW.h already present");
    }

    println!("project root: {}", p.root.display());
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

fn scan_protocols(p: &Paths) -> Result<(), Box<dyn Error>> {
    let root = wayland_protocols_root()?;
    let out = p.protocols();
    fs::create_dir_all(&out)?;

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
            None => return Err(format!("wayland protocol {name} not found in {}", root.display()).into()),
        };

        let header = out.join(format!("{name}.h"));
        let code = out.join(format!("{name}-protocol.c"));

        run(Command::new("wayland-scanner")
            .arg("client-header")
            .arg(&xml)
            .arg(&header))?;
        run(Command::new("wayland-scanner")
            .arg("private-code")
            .arg(&xml)
            .arg(&code))?;

        println!("protocols: {name} -> {}", header.display());
    }

    Ok(())
}

fn shadermake(p: &Paths) -> Result<(), Box<dyn Error>> {
    fs::create_dir_all(p.binshaders())?;
    let shaders = p.shaders();
    let out = p.binshaders();

    let mut sources: Vec<PathBuf> = if shaders.is_dir() {
        fs::read_dir(&shaders)?
            .filter_map(|e| e.ok().map(|e| e.path()))
            .filter(|f| {
                matches!(
                    f.extension().and_then(|s| s.to_str()),
                    Some("vert") | Some("frag") | Some("comp")
                )
            })
            .collect()
    } else {
        Vec::new()
    };
    sources.sort();

    if sources.is_empty() {
        println!("shadermake: no shader sources in {}", shaders.display());
        return Ok(());
    }

    for src in &sources {
        let stage = match src.extension().and_then(|s| s.to_str()) {
            Some("vert") => "vert",
            Some("frag") => "frag",
            _ => "comp",
        };
        let stem = src.file_stem().unwrap().to_string_lossy().to_string();
        let spv = out.join(format!("{stem}.spv"));
        let header = out.join(format!("{stem}.bin.h"));

        run(Command::new("glslc")
            .arg(format!("-fshader-stage={stage}"))
            .arg(src)
            .arg("-o")
            .arg(&spv))?;

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
        println!("shadermake: {} -> {}", src.display(), header.display());
    }
    Ok(())
}

fn compile(
    p: &Paths,
    compiler: &str,
    std: &str,
    unit: &PathBuf,
    extra: &[String],
) -> Result<PathBuf, Box<dyn Error>> {
    let name = unit
        .file_name()
        .map(|s| s.to_string_lossy().to_string())
        .unwrap_or_default();
    let stem = name.split('.').next().unwrap_or("unit").to_string();
    let obj = p.obj().join(format!("{stem}.o"));

    let flags: Vec<String> = vec![
        format!("-std={std}"),
        "-Wall".into(),
        "-Wextra".into(),
        "-Wno-unused-result".into(),
        "-O2".into(),
        "-g".into(),
        format!("-I{}", p.root.join("src").display()),
        format!("-I{}", p.external().display()),
        format!("-I{}", p.protocols().display()),
        format!("-I{}", p.binshaders().display()),
    ];

    println!("compiling {name}");
    run(Command::new(compiler)
        .args(&flags)
        .args(extra)
        .arg("-c")
        .arg(unit)
        .arg("-o")
        .arg(&obj))?;

    Ok(obj)
}

fn make(p: &Paths) -> Result<(), Box<dyn Error>> {
    shadermake(p)?;
    scan_protocols(p)?;

    fs::create_dir_all(p.obj())?;
    fs::create_dir_all(p.bin())?;

    let mut objs: Vec<PathBuf> = Vec::new();

    for unit in C_UNITS {
        objs.push(compile(p, "gcc", "c11", &p.root.join("src").join(unit), &[])?);
    }

    for entry in fs::read_dir(p.protocols())? {
        let path = entry?.path();
        if path.extension().and_then(|s| s.to_str()) == Some("c") {
            objs.push(compile(p, "gcc", "c11", &path, &[])?);
        }
    }

    for unit in CPP_UNITS {
        objs.push(compile(p, "g++", "c++20", &p.root.join("src").join(unit), &[])?);
    }

    let exe = p.bin().join("nah_engine");
    println!("linking {}", exe.display());

    let mut link = Command::new("g++");
    for obj in &objs {
        link.arg(obj);
    }
    for lib in LIBS {
        link.arg(format!("-l{lib}"));
    }
    link.arg("-o").arg(&exe);
    run(&mut link)?;

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
            "depfetch" => depfetch(&p)?,
            "shadermake" => shadermake(&p)?,
            "protocols" => scan_protocols(&p)?,
            "clean" => {
                if p.build().is_dir() {
                    fs::remove_dir_all(p.build())?;
                }
                println!("clean: removed {}", p.build().display());
            }
            other => {
                eprintln!("unknown command: {other}");
                eprintln!("usage: depfetch [make|depfetch|shadermake|protocols|clean]");
                std::process::exit(2);
            }
        }
    }
    Ok(())
}