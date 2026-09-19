use std::env;
use std::error::Error;
use std::path::PathBuf;
use std::process::Command;
use std::thread;

fn project_root() -> Result<PathBuf, Box<dyn Error>> {
    let cwd = env::current_dir()?;
    if cwd.join(".gitmodules").is_file() {
        return Ok(cwd);
    }
    if let Some(parent) = cwd.parent() {
        if parent.join(".gitmodules").is_file() {
            return Ok(parent.to_path_buf());
        }
    }
    if let Ok(exe) = env::current_exe() {
        let mut p = exe.as_path().parent();
        while let Some(dir) = p {
            if dir.join(".gitmodules").is_file() {
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

fn run(cmd: &mut Command) -> Result<(), Box<dyn Error>> {
    eprintln!("+ {:?}", cmd);
    let status = cmd.status()?;
    if !status.success() {
        return Err(format!("command failed: {:?}", cmd).into());
    }
    Ok(())
}

fn depfetch() -> Result<(), Box<dyn Error>> {
    let root = project_root()?;
    println!("project root: {}", root.display());
    let external = root.join("external");
    std::fs::create_dir_all(&external)?;
    let rgfw = external.join("RGFW.h");
    run(Command::new("curl")
        .arg("https://github.com/ColleagueRiley/RGFW/releases/download/1.8.1/RGFW.h")
        .arg("-o")
        .arg(&rgfw)
        .arg("-L"))?;
    let bgfx = external.join("bgfx");
    let bimg = external.join("bimg");
    let bx = external.join("bx");
    if !bgfx.is_dir() || !bimg.is_dir() || !bx.is_dir() {
        // NOTE: `git submodule --init` is invalid (init is a subcommand of update).
        run(Command::new("git")
            .arg("submodule")
            .arg("update")
            .arg("--init")
            .arg("--recursive")
            .current_dir(&root))?;
    } else {
        run(Command::new("git")
            .arg("submodule")
            .arg("update")
            .arg("--remote")
            .arg("--merge")
            .current_dir(&root))?;
    }
    Ok(())
}

fn depmake() -> Result<(), Box<dyn Error>> {
    let root = project_root()?;
    let bgfx = root.join("external/bgfx");
    if !bgfx.is_dir() {
        return Err("bgfx not found, run depfetch first".into());
    }
    if !bgfx.join("tools/bin/linux/shaderc").exists() {
        let jobs = thread::available_parallelism()
            .map(|n| n.to_string())
            .unwrap_or_else(|_| "4".to_string());
        run(Command::new("make")
            .arg("linux-gcc")
            .arg("-j")
            .arg(jobs)
            .current_dir(&bgfx))?;
    }
    println!("depmake: bgfx ready, libs at external/bgfx/.build/linux64_gcc/bin");
    Ok(())
}

fn make() -> Result<(), Box<dyn Error>> {
    let project_root = project_root()?;
    if !all(
        project_root.join("shaders/frag.bin.h").exists(),
        project_root.join("shaders/vert.bin.h").exists(),
    ) {
        shadermake()?;
    }

    let bin_dir = project_root.join("external/bgfx/.build/linux64_gcc/bin");
    let include_flags = [
        format!("-I{}", project_root.join("external/bgfx/include").display()),
        format!(
            "-I{}",
            project_root.join("external/bgfx/3rdparty").display()
        ),
        format!("-I{}", project_root.join("external/bgfx/bson").display()),
        format!("-I{}", project_root.join("external/bx/include").display()),
    ];

    let obj_dir = project_root.join("obj");
    std::fs::create_dir_all(&obj_dir)?;
    let bin_out = project_root.join("bin");
    std::fs::create_dir_all(&bin_out)?;
    let cwd = project_root.clone();

    println!("building main.cpp");
    let mut cmd = Command::new("g++");
    cmd.args(["-std=c++20", "-Wall", "-Wextra", "-O0", "-g"]);
    cmd.arg(format!("-I{}", project_root.join("src").display()));
    for f in &include_flags {
        cmd.arg(f);
    }
    cmd.arg("-DRGFW_VULKAN")
        .arg("-c")
        .arg(project_root.join("src/main.cpp"))
        .arg("-o")
        .arg(obj_dir.join("main.o"));
    let status = cmd.current_dir(&cwd).status()?;
    if !status.success() {
        return Err("g++ main.cpp fucked up".into());
    }

    println!("building rgfw.cpp");
    let mut cmd = Command::new("g++");
    cmd.args(["-std=c++20", "-Wall", "-Wextra", "-O0", "-g"]);
    cmd.arg(format!("-I{}", project_root.join("src").display()));
    cmd.arg(format!("-I{}", project_root.join("external").display()));
    for f in &include_flags {
        cmd.arg(f);
    }
    cmd.arg("-DRGFW_VULKAN")
        .arg("-c")
        .arg(project_root.join("src/rgfw.cpp"))
        .arg("-o")
        .arg(obj_dir.join("rgfw.o"));
    let status = cmd.current_dir(&cwd).status()?;
    if !status.success() {
        return Err("g++ rgfw.cpp fucked up".into());
    }

    println!("linking");
    let bgfx_libs = [
        bin_dir.join("libbgfxRelease.a"),
        bin_dir.join("libbxRelease.a"),
        bin_dir.join("libbimgRelease.a"),
    ];

    let mut cmd = Command::new("g++");
    cmd.arg(obj_dir.join("main.o"));
    cmd.arg(obj_dir.join("rgfw.o"));
    for lib in &bgfx_libs {
        cmd.arg(lib);
    }
    cmd.args([
        "-lX11",
        "-lXrandr",
        "-lXext",
        "-lvulkan",
        "-ldl",
        "-lpthread",
        "-o",
    ]);
    cmd.arg(bin_out.join("nah_engine"));
    let status = cmd.current_dir(&cwd).status()?;
    if !status.success() {
        return Err("linking fucked up".into());
    }

    println!("make: done");
    Ok(())
}

fn shadermake() -> Result<(), Box<dyn Error>> {
    todo!("shadermake");
}

fn main() -> Result<(), Box<dyn Error>> {
    let args: Vec<String> = env::args().collect();

    if args.len() < 2 {
        depfetch()?;
    } else if args[1] == "make" {
        make()?;
    } else if args[1] == "depmake" {
        depmake()?;
    } else if args[1] == "shadermake" {
        shadermake()?;
    } else {
        eprintln!("usage: depfetch [make|depmake|shadermake]");
        std::process::exit(2);
    }
    Ok(())
}
