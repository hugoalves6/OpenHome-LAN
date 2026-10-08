use serde::{Deserialize, Serialize};
use sha2::{Digest, Sha256};
use std::{fs, process::Command};
use tauri::{AppHandle, Manager};

const CURRENT_LAN_RELEASE: &str = "0.3.1";
const RELEASES_URL: &str = "https://api.github.com/repos/hugoalves6/OpenHome-LAN/releases";

#[derive(Deserialize)]
struct ReleaseAsset {
    name: String,
    browser_download_url: String,
    digest: Option<String>,
}

#[derive(Deserialize)]
struct Release {
    tag_name: String,
    draft: bool,
    assets: Vec<ReleaseAsset>,
}

#[derive(Serialize)]
pub struct LanUpdate {
    available: bool,
    version: Option<String>,
}

async fn newest_release() -> Result<Option<Release>, String> {
    let client = reqwest::Client::builder()
        .user_agent("OpenHome-LAN-updater")
        .build()
        .map_err(|e| e.to_string())?;
    let releases: Vec<Release> = client
        .get(RELEASES_URL)
        .send()
        .await
        .map_err(|_| "Could not check for updates")?
        .error_for_status()
        .map_err(|_| "Could not check for updates")?
        .json()
        .await
        .map_err(|_| "Update information was invalid")?;
    Ok(releases.into_iter().find(|release| !release.draft && release.tag_name.starts_with("lan-v")))
}

fn release_version(release: &Release) -> Result<semver::Version, String> {
    semver::Version::parse(release.tag_name.trim_start_matches("lan-v"))
        .map_err(|_| "Update version was invalid".into())
}

#[tauri::command]
pub async fn check_lan_update() -> Result<LanUpdate, String> {
    let Some(release) = newest_release().await? else {
        return Ok(LanUpdate { available: false, version: None });
    };
    let available = release_version(&release)? > semver::Version::parse(CURRENT_LAN_RELEASE).unwrap();
    Ok(LanUpdate {
        available,
        version: available.then(|| release.tag_name.trim_start_matches("lan-v").to_owned()),
    })
}

#[tauri::command]
pub async fn install_lan_update(app: AppHandle) -> Result<(), String> {
    let Some(release) = newest_release().await? else { return Err("No update is available".into()) };
    if release_version(&release)? <= semver::Version::parse(CURRENT_LAN_RELEASE).unwrap() {
        return Err("OpenHome LAN is already up to date".into());
    }
    let tag_name = release.tag_name.clone();
    let asset = release.assets.into_iter().find(|asset| asset.name.ends_with("windows-x64-setup.exe"))
        .ok_or("Update installer was missing")?;
    let expected = asset.digest.as_deref().and_then(|value| value.strip_prefix("sha256:"))
        .ok_or("Update installer checksum was missing")?;
    let bytes = reqwest::Client::builder().user_agent("OpenHome-LAN-updater").build()
        .map_err(|e| e.to_string())?.get(&asset.browser_download_url).send().await
        .map_err(|_| "Could not download the update")?.error_for_status()
        .map_err(|_| "Could not download the update")?.bytes().await
        .map_err(|_| "Could not download the update")?;
    let actual = format!("{:x}", Sha256::digest(&bytes));
    if actual != expected { return Err("Update checksum did not match".into()); }
    let installer = std::env::temp_dir().join(format!("OpenHome-LAN-{tag_name}.exe"));
    fs::write(&installer, &bytes).map_err(|_| "Could not save the update installer")?;
    Command::new(&installer).arg("/S").spawn().map_err(|_| "Could not start the update installer")?;
    app.exit(0);
    Ok(())
}
