use std::error::Error;
use std::process::Command;

const URLS: &[&str] = &["https://github.com/ColleagueRiley/RGFW/releases/download/1.8.1/RGFW.h"];

fn main() -> Result<(), Box<dyn Error>> {
    for u in URLS {
        let _ = Command::new("curl")
            .arg(u)
            .arg("-o")
            .arg("../external/RGFW.h")
            .arg("-L")
            .status();
    }
    Ok(())
}
