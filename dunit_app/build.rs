use std::env;
use std::path::Path;

fn main() {
    let manifest_dir = env::var("CARGO_MANIFEST_DIR").unwrap();
    let repo_dir = Path::new(&manifest_dir).parent().unwrap();
    let dunit_build = repo_dir.join("dunit_build");
    println!("cargo:rustc-link-search=native={}", dunit_build.display());

    let sysroot_lib = env::var("DUNIT_SYSROOT")
        .map(|s| format!("{}/lib", s))
        .unwrap_or_else(|_| "/home/tuxpc/dunit-os/build/dunit-sysroot/lib".to_string());
    println!("cargo:rustc-link-search=native={}", sysroot_lib);
    println!("cargo:rustc-link-lib=static=mced_core");
    println!("cargo:rustc-link-lib=static=c");
}
