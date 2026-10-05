//! Narrow LAN HTTP transport for the local hub; credentials never reach logs.
use serde_json::Value;
use std::time::Duration;

#[tauri::command]
pub async fn lan_hub_request(
    base: String,
    route: String,
    token: Option<String>,
    body: Option<Value>,
) -> Result<Value, String> {
    let url = reqwest::Url::parse(&base).map_err(|_| "Invalid hub address")?;
    let host = url.host_str().unwrap_or_default();
    let parts: Vec<_> = host.split('.').collect();
    if url.scheme() != "http" || parts.len() != 4 || parts[..3] != ["192", "168", "1"]
        || !parts[3].parse::<u8>().is_ok_and(|n| n > 0 && n < 255)
        || !url.username().is_empty() || url.password().is_some()
        || url.query().is_some() || url.fragment().is_some() || url.path() != "/"
    {
        return Err("Use a LAN hub address such as http://192.168.1.125:8321".into());
    }
    if !matches!(route.as_str(), "/ohnx/login" | "/ohnx/live/status" | "/ohnx/live/acquire"
        | "/ohnx/live/renew" | "/ohnx/live/release" | "/ohnx/live/commit") {
        return Err("Unsupported hub request".into());
    }
    let client = reqwest::Client::builder().timeout(Duration::from_secs(20))
        .redirect(reqwest::redirect::Policy::none()).build().map_err(|e| e.to_string())?;
    let endpoint = url.join(&route).map_err(|e| e.to_string())?;
    let mut request = if body.is_some() { client.post(endpoint) } else { client.get(endpoint) };
    if let Some(token) = token { request = request.header("X-Auth", token); }
    if let Some(body) = body {
        let encoded = serde_json::to_vec(&body).map_err(|e| e.to_string())?;
        if encoded.len() > 750000 { return Err("Save request too large".into()); }
        request = request.header("Content-Type", "application/json").body(encoded);
    }
    let mut response = request.send().await.map_err(|_| "Hub unavailable".to_string())?;
    let status = response.status();
    let mut bytes = Vec::new();
    while let Some(chunk) = response.chunk().await.map_err(|_| "Incomplete hub response")? {
        if bytes.len() + chunk.len() > 1500000 { return Err("Hub response too large".into()); }
        bytes.extend_from_slice(&chunk);
    }
    let data: Value = serde_json::from_slice(&bytes).map_err(|_| "Invalid hub response")?;
    if !status.is_success() {
        return Err(data["error"].as_str().unwrap_or("Hub rejected request").to_string());
    }
    Ok(data)
}
