#[macro_use]
extern crate log;
extern crate clap;
extern crate env_logger;

use clap::{App, Arg};
use core::time;
use encoding_rs::SHIFT_JIS;
use log::LevelFilter;
use regex::Regex;
use std::{io::Read, thread::sleep};
use urlencoding::encode;

use bindings::Windows::Storage::Streams::InMemoryRandomAccessStream;
use bindings::Windows::{
    Globalization::Language,
    Graphics::Imaging::{
        BitmapAlphaMode, BitmapDecoder, BitmapPixelFormat, BitmapTransform, ColorManagementMode,
        ExifOrientationMode,
    },
    Media::Ocr::OcrEngine,
    Storage::Streams::DataWriter,
};

const BASE: &str = "https://member.gungho.jp";
const LOGIN_URL: &str = "/front/ro/iframe/login.aspx";
const IMAGE_AUTH: &str = "/front/ro/iframe/redirectimageauth.aspx";
const IMAGE_AUTH2: &str = "/front/ro/guest/imageauth.aspx";
const IMAGE_AUTH_IMAGE: &str = "/front/register/JpegImage.aspx";
const IVR_AUTH: &str = "/front/safetylock/ivrauth.aspx";
const WEBGS: &str = "/front/ro/iframe/menu.aspx";

fn get_view_state(html: &str) -> Result<&str, Box<dyn std::error::Error>> {
    let regex = Regex::new(r#"name="__VIEWSTATE" id="__VIEWSTATE" value="(.+)""#)?;
    let ret = match regex.captures(html) {
        Some(v) => v.get(1).map_or("", |m| m.as_str()),
        None => "",
    };
    Ok(ret)
}

fn main() -> Result<(), Box<dyn std::error::Error>> {
    env_logger::builder().filter_level(LevelFilter::Info).init();

    let matches = App::new("rologin")
        .arg(
            Arg::with_name("gungho-id")
                .long("id")
                .required(true)
                .takes_value(true),
        )
        .arg(
            Arg::with_name("gungho-pw")
                .long("pw")
                .required(true)
                .takes_value(true),
        )
        .arg(
            Arg::with_name("attraction-id")
                .long("aid")
                .required(true)
                .takes_value(true),
        )
        .arg(
            Arg::with_name("otp-token")
                .long("otp")
                .required(false)
                .takes_value(true),
        )
        .arg(
            Arg::with_name("path-to-roexeuri")
                .long("path")
                .required(false)
                .takes_value(true),
        )
        .get_matches();

    let id = matches.value_of("gungho-id").unwrap_or("");
    let pw = matches.value_of("gungho-pw").unwrap_or("");
    let aid = matches.value_of("attraction-id").unwrap_or("");
    let otp = matches.value_of("otp-token").unwrap_or("");
    let path = matches.value_of("path-to-roexeuri").unwrap_or("");
    info!("id={} pw={} aid={} otp={} path={}", id, pw, aid, otp, path);

    loop {
        match login(id, pw, aid, otp) {
            Ok(true) => {
                info!("正常終了");
                break;
            }
            Ok(false) => {
                info!("ログインに失敗しました。3秒後に再試行します。");
                sleep(time::Duration::from_secs(3));
            }
            Err(v) => {
                error!("ログイン中に例外が発生しました。3秒後に再試行します。({})", v);
                sleep(time::Duration::from_secs(3));
            }
        }
    }

    std::process::exit(0);
}

fn login(id: &str, pw: &str, aid: &str, otp: &str) -> Result<bool, Box<dyn std::error::Error>> {
    let agent = ureq::AgentBuilder::new()
        .user_agent("Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/74.0.3729.169 Safari/537.36")
        .redirects(0)
        .build();

    // VIEWSTATE取得
    let html = agent
        .get(&format!("{}{}", BASE, LOGIN_URL))
        .call()?
        .into_string()?;
    let viewstate = get_view_state(&html).expect("VIEWSTATEの取得に失敗しました。");

    // ログイン試行
    let url = &format!("{}{}", BASE, LOGIN_URL);
    let res = agent
        .post(url)
        .set("content-type", "application/x-www-form-urlencoded")
        .set("referer", &LOGIN_URL)
        .send_form(&[
            ("__EVENTTARGET", ""),
            ("__EVENTARGUMENT", ""),
            ("__VIEWSTATE", viewstate),
            ("loginNameControl$txtLoginName", id),
            ("login", ""),
            ("passwordControl$txtPassword", pw),
            ("OTPControl$inputOTP", otp),
        ])?;

    // リダイレクト先取得
    let status = res.status();
    if status != 302 {
        panic!("予期しないステータスコードが返されました。({}).", status);
    }
    let redirect = res
        .header("location")
        .unwrap_or("")
        .to_string()
        .replace("%2f", "/")
        .replace("%3a", ":");

    // 認証処理
    let mut location = redirect;
    let mut relogin = false;
    loop {
        if relogin {
            return Ok(false);
        }

        if location.starts_with(&WEBGS) {
            info!("アカウント一覧取得開始 ...");
            let url = &format!("{}{}", BASE, WEBGS);
            let res = agent.get(&url).call()?;
            let html = res.into_string()?;
            let document = scraper::Html::parse_document(&html);
            let selector = scraper::Selector::parse("#ddlCPID option").unwrap();
            let elems = document.select(&selector);
            for elem in elems {
                let siid = elem.value().attr("value").unwrap();
                let text = elem.text().next().unwrap();
                let regex = Regex::new(r"(.*)\((.*)\)")?;
                if let Some(captures) = regex.captures(text) {
                    if captures.len() < 3 {
                        // "ゲームアカウントを設定してください"
                        continue;
                    }

                    let left = &captures[1];
                    let right = &captures[2];
                    if left == aid || right == aid {
                        // アトラクションID決定
                        info!(
                            "アトラクションIDが見つかりました。({}: {}({}))",
                            siid, left, right
                        );

                        // SIIDからアトラクションパスワード取得
                        let url = &format!("{}{}?SIID={}", BASE, WEBGS, siid);
                        let mut res = agent.get(&url).call()?;
                        if res.status() == 302 {
                            let url = &format!("{}{}", BASE, res.header("location").unwrap());
                            info!("redirect={}", url);
                            res = agent.get(&url).call()?;
                        }

                        let html = res.into_string()?;
                        if html.contains("利用券の購入が必要です") {
                            panic!("アトラクションIDの利用権がありません。");
                        }

                        let regex = Regex::new(r"GameStartAsync\('(.*)'\)")?;
                        let captures = regex
                            .captures(&html)
                            .expect(format!("GameStartAsyncの取得に失敗しました html={}", html).as_str());
                        let pw = &captures[1];

                        let cmd = format!("ROEXEURI://-w^&{}", pw);
                        info!("ゲームクライアント起動開始 ... ({})", cmd);
                        let _ = std::process::Command::new("cmd")
                            .args(vec!["/c", "start", "", &cmd])
                            .spawn()?
                            .wait()?;

                        // 正常終了
                        return Ok(true);
                    }
                }
            }
            panic!("アトラクションIDが見つかりませんでした。")
        } else if location.starts_with(&IVR_AUTH) {
            info!("IVR認証開始 ...");
        } else if location.starts_with(&IMAGE_AUTH) {
            info!("画像認証開始 ...");

            // 画像認証ページまでリダイレクト
            let regex = Regex::new(r"aspx\?otp=(.*?)&")?;
            let captures = regex
                .captures(&location)
                .expect(&format!("URLからOTPの取得に失敗しました。({})", location));
            let otp = &captures[1];
            info!("session otp={}", otp);
            let url1 = &format!("{}{}?otp={}&.goeReturnUrl=/front/member/center.aspx&.goeBackUrl=/front/member/center.aspx", BASE, IMAGE_AUTH, &otp);
            let url2 = &format!("{}{}?otp={}&.goeReturnUrl=/front/member/center.aspx&.goeBackUrl=/front/member/center.aspx", BASE, IMAGE_AUTH2, &otp);
            let _res1 = agent.get(&url1).call()?;
            let res2 = agent.get(&url2).call()?;
            let html = res2.into_string()?;
            let viewstate = get_view_state(&html).expect("VIEWSTATEの取得に失敗しました。");

            let chars = image_ocr(&agent)?;
            let count = chars.chars().count();
            if count != 4 {
                error!(
                    "画像認識に失敗しました。文字数が不正です。 text({}) count({})",
                    chars, count
                );
                relogin = true;
                info!("画像認証を3秒後に再試行します。");
                std::thread::sleep(time::Duration::from_secs(3));
                continue;
            }

            let success = image_auth(&agent, &url2, &viewstate, &chars)?;
            if success {
                let store = &agent.cookie_store();
                let c: String = store
                    .iter_any()
                    .filter(|m| m.name().starts_with("GHLI"))
                    .nth(0)
                    .expect("画像認証に失敗しました。")
                    .value()
                    .to_owned();
                info!("画像認証に成功しました。({})", c);

                // アカウント一覧へ移動
                location = WEBGS.to_string();
            } else {
                relogin = true;
                info!("画像認証を3秒後に再試行します。");
                std::thread::sleep(time::Duration::from_secs(3));
                continue;
            }
        } else {
            panic!("予期しないリダイレクトが発生しました。({}).", location);
        }
    }
}

fn image_ocr(agent: &ureq::Agent) -> Result<String, Box<dyn std::error::Error>> {
    // 画像URL読込
    let url_image = &format!("{}{}", BASE, IMAGE_AUTH_IMAGE);
    let req_image = agent.get(&url_image);
    let res_image = req_image.call()?;
    let mut reader = res_image.into_reader();
    let mut bytes = vec![];
    reader.read_to_end(&mut bytes)?;

    // OCR
    let memory_stream = InMemoryRandomAccessStream::new()?;
    let output_stream = memory_stream.GetOutputStreamAt(0)?;
    let data_writer = DataWriter::CreateDataWriter(output_stream)?;
    data_writer.WriteBytes(&bytes.to_vec())?;
    data_writer.StoreAsync()?;
    data_writer.FlushAsync()?.Close()?;
    let decoder =
        BitmapDecoder::CreateWithIdAsync(BitmapDecoder::JpegDecoderId()?, memory_stream)?.get()?;
    let transform = BitmapTransform::new()?;
    let bitmap = decoder
        .GetSoftwareBitmapTransformedAsync(
            BitmapPixelFormat::Rgba8,
            BitmapAlphaMode::Premultiplied,
            transform,
            ExifOrientationMode::IgnoreExifOrientation,
            ColorManagementMode::DoNotColorManage,
        )?
        .get()?;

    let lang = Language::CreateLanguage("ja-JP")?;
    let engine = OcrEngine::TryCreateFromLanguage(lang)?;
    let result = engine.RecognizeAsync(bitmap.clone())?.get()?;
    let text = result.Text()?;
    let trimmed = text.to_string().replace(" ", "");
    return Ok(trimmed);
}

fn image_auth(
    agent: &ureq::Agent,
    url: &str,
    viewstate: &str,
    chars: &str,
) -> Result<bool, Box<dyn std::error::Error>> {
    let (cow, _, err) = SHIFT_JIS.encode(&chars);
    if err {
        panic!("SHIFT_JIS 変換に失敗しました。");
    }
    let data = &cow[..];
    let v: String = url::form_urlencoded::byte_serialize(data)
        .map(|m| m.chars().to_owned())
        .flatten()
        .collect();

    let mut kv: Vec<String> = Vec::new();
    kv.push("__LASTFOCUS=".to_string());
    kv.push("__EVENTTARGET=".to_string());
    kv.push("__EVENTARGUMENT=".to_string());
    kv.push("__VIEWSTATE=".to_string() + &encode(viewstate));
    kv.push(encode("ctl00$ctl00$MainContent$TopContent$captchaControlAjax$txtCaptcha") + "=" + &v);
    kv.push(encode("ctl00$ctl00$MainContent$TopContent$chbSave") + "=on");
    kv.push(encode("ctl00$ctl00$MainContent$TopContent$txt") + "=");
    kv.push(encode("ctl00$ctl00$MainContent$TopContent$btnNext") + "=");
    let u8 = kv.join("&");
    let res3 = agent
        .post(url)
        .set("Content-Type", "application/x-www-form-urlencoded")
        .set("Referer", url)
        .set("cache-control", "max-age=0")
        .send_string(&u8)?;

    if res3.status() == 302 {
        Ok(true)
    } else {
        Ok(false)
    }
}
