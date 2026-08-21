#include "globals.h"
#include "html.h"
#include "relay.h"
#include <Arduino.h>

// F7: escape operator-controlled config values before interpolating them into an HTML
// attribute value. Centralized here (single call site edited if the escape set ever needs
// to change) instead of inlined at each of the 9 interpolation points below - a bare
// apostrophe typed into e.g. mqttServer would otherwise break out of the surrounding
// value='...' attribute and corrupt the rendered form. Order matters: '&' must be escaped
// first so it doesn't double-escape the entities produced for the other four characters.
static String htmlEscape(const String& s)
{
  String out;
  out.reserve(s.length());
  for (size_t i = 0; i < s.length(); i++) {
    char c = s[i];
    switch (c) {
      case '&':  out += "&amp;";  break;
      case '<':  out += "&lt;";   break;
      case '>':  out += "&gt;";   break;
      case '"':  out += "&quot;"; break;
      case '\'': out += "&#39;";  break;
      default:   out += c;        break;
    }
  }
  return out;
}

void handleRoot()
{
  String html;
  // Trang nay dai ~13KB. Khong reserve() thi day la hon chuc lan realloc+memcpy tang dan moi
  // lan mo trang, moi lan bo lai mot lo block chet giua heap.
  html.reserve(16384);

  html += "<!DOCTYPE html>";
  html += "<html>";
  html += "<head>";
  html += "<meta charset='utf-8'>";
  html += "<meta name='viewport' content='width=device-width,initial-scale=1'>";
  html += "<style>";
  html += "*{box-sizing:border-box;}";
  html += "body{margin:0;padding:15px;background:#f4f7fb;font-family:Arial,Helvetica,sans-serif;color:#233244;}";
  html += ".card{max-width:980px;margin:auto;background:#fff;padding:24px;border-radius:18px;box-shadow:0 6px 20px rgba(0,0,0,.12);}";
  html += "h2{text-align:center;margin:0 0 16px;color:#1565C0;}";
  html += "h3{margin:22px 0 12px;color:#1565C0;border-bottom:2px solid #dbeafe;padding-bottom:6px;}";
  html += ".panel{background:#f8fbff;border:1px solid #dbeafe;border-radius:12px;padding:14px;margin-bottom:14px;}";
  // Tieu de panel keo sat 3 mep tren/trai/phai cua o: margin am PHAI dung bang padding cua
  // .panel (14px) - doi padding do ma quen dong nay thi tieu de se thut vao hoac tran ra ngoai
  // vien. Padding bu lai de chu van thang cot voi cac truong ben duoi, chi rieng duong gach
  // chan (border-bottom o luat h3 chung) la chay het be ngang lam vach ngan cua panel.
  html += ".panel h3{margin:-14px -14px 12px;padding:10px 14px 8px;}";
  // Panel xo ra/thu vao: <summary> phai trong y het <h3> cua panel thuong, ke ca margin am
  // -14px an khop voi padding cua .panel (xem ghi chu o tren).
  html += ".panel details>summary{font-size:17px;font-weight:bold;color:#1565C0;border-bottom:2px solid #dbeafe;";
  html += "margin:-14px -14px 12px;padding:10px 14px 8px;cursor:pointer;user-select:none;border-radius:11px 11px 0 0;}";
  html += ".panel details>summary:hover{background:#eef5ff;}";
  // Luc DONG, summary la toan bo noi dung panel: keo margin-bottom am de nuot not padding day
  // cua .panel (khong thi thua 26px trong tron duoi tieu de), bo gach chan de khong chong len
  // vien duoi cua panel thanh 2 vach, va bo tron ca 4 goc cho nen hover khong tran ra ngoai.
  html += ".panel details:not([open])>summary{margin-bottom:-14px;border-bottom:none;border-radius:11px;}";
  html += ".sensor{border:1px solid #d8e3f0;border-radius:12px;padding:12px;margin-bottom:12px;background:#fbfdff;}";
  html += ".sensor-title{font-weight:bold;color:#0f4c81;margin-bottom:8px;}";
  html += ".row{display:flex;gap:12px;flex-wrap:wrap;margin-top:8px;}";
  html += ".field{flex:1;min-width:220px;}";
  // Tieu de "Sensor N" khi nam trong .row: khong gian ra, khong co dan theo .field, va can
  // GIUA theo chieu doc - cac .field cao hon no (nhan + o nhap) nen mac dinh align-items:
  // stretch se dan chu len sat dinh, nhin nhu bi lech mot dong.
  html += ".row .sensor-title{flex:0 0 auto;min-width:74px;margin-bottom:0;align-self:center;}";
  // .row co margin-top:8px cho truong hop nam duoi mot dong khac; trong .sensor no la phan tu
  // dau tien nen 8px do cong voi padding cua .sensor lam le tren day hon le duoi.
  html += ".sensor .row{margin-top:0;}";
  // O tick Enable chi co DUY NHAT mot nhan, khong co o nhap ben duoi nhu .field MIN/MAX - de
  // mac dinh thi no dinh len dinh o va lech han so voi 2 o nhap ben canh. Bien chinh .field
  // thanh flex de can giua ca 2 chieu; margin-bottom cua nhan phai bo, khong thi phan can giua
  // theo chieu doc bi keo len 6px.
  html += ".field.enable{display:flex;align-items:center;justify-content:center;}";
  html += ".field.enable label{margin-bottom:0;}";
  // Nhan MIN/MAX can giua tren o nhap cua chinh no. Gioi han trong .sensor: cac nhan khac cua
  // form (MQTT IP, OSC Port...) van can trai nhu cu.
  html += ".sensor .field label{text-align:center;}";
  html += ".field label,.single label{display:block;font-weight:bold;margin-bottom:6px;color:#556270;font-size:14px;}";
  html += ".field input,.single input{width:100%;padding:10px;border:1px solid #bfc9d6;border-radius:8px;font-size:15px;background:#fff;}";
  // type=radio phai duoc mien tru y het type=checkbox: luat .single input{width:100%;padding:10px;
  // border:...} o tren ap cho MOI input, radio khong duoc liet ke o day se bi keo rong ca dong
  // kem padding/vien, chu nhan day lech han sang mot ben.
  html += ".field input[type=checkbox],.single input[type=checkbox],";
  html += ".field input[type=radio],.single input[type=radio]{width:auto;padding:0;margin-right:6px;transform:translateY(2px);}";
  // Nhan cua tung lua chon (radio/checkbox trong mot nhom) - khong dam, de phan biet voi nhan
  // TIEU DE cua ca nhom von dam theo luat .single label o tren.
  html += ".single label.opt{font-weight:normal;margin-bottom:4px;}";
  html += ".single{margin:10px 0;}";
  html += "#d{background:#eef7ff;border-left:5px solid #2196F3;padding:12px;border-radius:10px;margin-bottom:18px;line-height:1.7;}";
  html += ".btn{width:100%;padding:14px;border:none;border-radius:10px;background:#2196F3;color:white;font-size:16px;font-weight:bold;cursor:pointer;margin-top:8px;}";
  html += ".btn:hover{background:#1976D2;}";
  html += ".note{font-size:13px;color:#64748b;margin-top:6px;}";
  html += ".tabs{display:flex;gap:8px;margin-bottom:16px;}";
  html += ".tab-btn{flex:1;padding:12px;border:none;border-radius:10px;background:#dbeafe;color:#1565C0;font-size:15px;font-weight:bold;cursor:pointer;}";
  html += ".tab-btn.active{background:#2196F3;color:#fff;}";
  html += ".tab-content{display:none;}";
  html += ".tab-content.active{display:block;}";
  html += "@media(max-width:600px){";
  html += ".card{padding:16px;}";
  html += ".field{min-width:100%;}";
  html += "h2{font-size:24px;}";
  html += "}";
  html += "</style>";
  html += "<script>";
  html += "function update(){";
  html += "fetch('/data')";
  html += ".then(r=>r.text())";
  html += ".then(t=>document.getElementById('d').innerHTML=t);";
  html += "}";
  html += "setInterval(update,250);";
  // Log KHONG tu tai theo chu ky: no ~4KB moi lan, ma dashboard da poll lien tuc roi. Tai mot
  // lan luc mo trang (du de xem dien bien khoi dong) roi de nut bam lo cac lan sau.
  html += "function loadLog(){var b=document.getElementById('logbox');b.textContent='Đang tải...';"
          "fetch('/log').then(r=>{if(!r.ok)throw new Error('HTTP '+r.status);return r.text();})"
          ".then(t=>{b.textContent=t;b.scrollTop=b.scrollHeight;})"
          ".catch(e=>{b.textContent='Không đọc được log: '+e.message;});}";
  html += "window.onload=function(){update();loadLog();};";
  html += "function showTab(name){";
  html += "document.querySelectorAll('.tab-content').forEach(e=>e.classList.remove('active'));";
  html += "document.querySelectorAll('.tab-btn').forEach(e=>e.classList.remove('active'));";
  html += "document.getElementById('tab-'+name).classList.add('active');";
  html += "document.getElementById('btn-'+name).classList.add('active');";
  html += "}";
  html += "</script>";
  html += "</head>";
  html += "<body>";
  html += "<div class='card'>";
  html += "<h2>NGHI LỄ CÂN TIM</h2>";
  html += "<div id='d'>Loading...</div>";
  html += "<div class='tabs'>";
  html += "<button type='button' id='btn-general' class='tab-btn active' onclick=\"showTab('general')\">Cấu hình</button>";
  html += "<button type='button' id='btn-network' class='tab-btn' onclick=\"showTab('network')\">Mạng (Ethernet)</button>";
  html += "</div>";
  html += "<form action='/save' method='POST'>";
  html += "<div id='tab-general' class='tab-content active'>";
  html += "<div class='panel'>";
  html += "<h3>Sensor Configuration</h3>";
  html += "<div class='single'>";
  html += "<label>Nguồn RS485</label>";
  html += "<label class='opt'><input type='radio' name='rs485_src' value='main'";
  html += rs485UseBackup ? "" : " checked";
  html += "> Module chính (RX = GPIO ";
  html += RS485_RX_MAIN;
  html += ")</label>";
  html += "<label class='opt'><input type='radio' name='rs485_src' value='backup'";
  html += rs485UseBackup ? " checked" : "";
  html += "> Module dự phòng (RX = GPIO ";
  html += RS485_RX_BACKUP;
  html += ")</label>";
  html += "</div>";
  html += "<div class='note'>Cả 3 sensor đọc từ module đang chọn, Save là áp dụng ngay (không cần reboot); chuyển sang module chưa cắm dây thì sau 5 giây cả 3 sensor báo OFFLINE.</div>";
  for (int i = 0; i < DEVICE_NUM; i++) {
    html += "<div class='sensor'>";
    // Tieu de nam TRONG .row (khong phai mot dong rieng ben tren) de "Sensor N" thang hang voi
    // o Enable/MIN/MAX cua chinh no - moi sensor gon lai con 1 dong thay vi 2.
    html += "<div class='row'>";
    html += "<div class='sensor-title'>Sensor ";
    html += i + 1;
    html += "</div>";
    html += "<div class='field enable'>";
    html += "<label><input type='checkbox' name='sensor";
    html += i;
    html += "' value='1'";
    html += sensorEnabled[i] ? " checked" : "";
    html += "> Enable this sensor for publish</label>";
    html += "</div>";
    html += "<div class='field'>";
    html += "<label>MIN Distance (mm)</label>";
    html += "<input name='min";
    html += i;
    html += "' value='";
    html += distanceMin[i];
    html += "'>";
    html += "</div>";
    html += "<div class='field'>";
    html += "<label>MAX Distance (mm)</label>";
    html += "<input name='max";
    html += i;
    html += "' value='";
    html += distanceMax[i];
    html += "'>";
    html += "</div>";
    html += "</div>";
    html += "</div>";
  }
  html += "<div class='note'>Enable only the sensors that should be required for the FULL condition.</div>";
  html += "<div class='single'>";
  // "Ngoai range" chu KHONG phai "rot": sensor OFFLINE bi loai han khoi phep dem, khong tinh
  // vao con so nay. Nhan cu ghi "rot" de nguoi van hanh hieu la gom ca rot mang - doc nhan ma
  // suy ra hanh vi thi ra sai.
  //
  // Nhan phai noi ro "NGAT FULL": tu 2026-08-10 con so nay CHI dieu khien chieu ra (xem
  // hysteresis trong checkDistance()). Chieu vao FULL luon doi du HET, khong cau hinh duoc.
  html += "<label>Số sensor ra khỏi range thì NGẮT FULL (1-";
  html += DEVICE_NUM;
  html += ")</label>";
  html += "<input name='miss_thresh' value='";
  html += missingThreshold;
  html += "'>";
  html += "</div>";
  html += "<div class='note'><b>Vào FULL:</b> phải TẤT CẢ sensor đang Enable + online đều trong range - không cấu hình được. <b>Ngắt FULL:</b> khi số sensor ra khỏi range đạt con số trên. Ở giữa thì giữ nguyên trạng thái đang có, nên đặt 2 nghĩa là \"đủ hết mới lên FULL, nhưng 1 con lệch tạm thì chưa cắt cue\". Đặt 1 = không có vùng đệm, ra khỏi range 1 con là MISSING ngay.</div>";
  html += "<div class='note'>Sensor OFFLINE bị loại hẳn khỏi phép đếm, KHÔNG tính là ngoài range - nên mất 1 sensor thì tiêu chuẩn vào FULL tự hạ theo (chỉ cần các con còn sống đều trong range). Nếu số sensor online ít hơn ngưỡng thì ngưỡng tự hạ xuống bằng số đó.</div>";
  html += "</div>";
  html += "<div class='panel'>";
  html += "<h3>Confirm Settings</h3>";
  html += "<div class='single'>";
  html += "<label>Confirm Time - FULL (ms)</label>";
  html += "<input name='confirm' value='";
  html += confirmTime;
  html += "'>";
  html += "</div>";
  html += "<div class='single'>";
  html += "<label>Confirm Time - MISSING (ms)</label>";
  html += "<input name='confirm_miss' value='";
  html += confirmTimeMissing;
  html += "'>";
  html += "</div>";
  html += "<div class='note'>Thời gian trạng thái phải giữ ổn định trước khi publish, tách riêng FULL/MISSING, giới hạn 50-60000ms.</div>";
  html += "<div class='single'>";
  html += "<label>Heartbeat - gửi lại cue định kỳ (ms, 0 = tắt)</label>";
  html += "<input name='heartbeat' value='";
  html += heartbeatInterval;
  html += "'>";
  html += "</div>";
  html += "<div class='note'>Định kỳ bắn lại cue hiện tại để bù gói MQTT/OSC bị rớt, không đổi máy trạng thái - chỉ nhắc lại. 0 = tắt, giới hạn 5000-3600000ms.</div>";
  html += "</div>";
  html += "<div class='panel'>";
  // <details>/<summary>: xo ra/thu vao khong can mot dong JS nao. Quan trong: cac o nhap nam
  // trong <details> DANG DONG van thuoc form va van duoc gui khi bam Save - dong lai chi la an
  // hien thi, khong go khoi DOM. Neu doi sang cach khac (vd tu xoa/them node) thi phai kiem
  // lai diem nay, khong thi Save se am tham lam rong cau hinh MQTT/OSC.
  html += "<details class='acc'>";
  html += "<summary>MQTT Settings</summary>";
  html += "<div class='single'>";
  html += "<label><input type='checkbox' name='mqtt_enable' value='1'";
  html += mqttEnabled ? " checked" : "";
  html += "> Enable MQTT</label>";
  html += "</div>";
  html += "<div class='single'>";
  html += "<label>MQTT IP</label>";
  html += "<input name='mqtt_ip' value='";
  html += htmlEscape(mqttServer);
  html += "'>";
  html += "</div>";
  html += "<div class='single'>";
  html += "<label>MQTT Port</label>";
  html += "<input name='mqtt_port' value='";
  html += mqttPort;
  html += "'>";
  html += "</div>";
  html += "<div class='single'>";
  html += "<label>Username</label>";
  html += "<input name='mqtt_user' value='";
  html += htmlEscape(mqttUser);
  html += "'>";
  html += "</div>";
  html += "<div class='single'>";
  html += "<label>Password (để trống = giữ nguyên)</label>";
  html += "<input type='password' name='mqtt_pass' placeholder='(giữ nguyên nếu để trống)'>";
  html += "</div>";
  html += "<div class='single'>";
  html += "<label>MQTT Topic</label>";
  html += "<input name='mqtt_topic' value='";
  html += htmlEscape(mqttTopic);
  html += "'>";
  html += "</div>";
  html += "<div class='single'>";
  html += "<label>FULL Message</label>";
  html += "<input name='mqtt_full' value='";
  html += htmlEscape(mqttFullValue);
  html += "'>";
  html += "</div>";
  html += "<div class='single'>";
  html += "<label>MISSING Message</label>";
  html += "<input name='mqtt_missing' value='";
  html += htmlEscape(mqttMissingValue);
  html += "'>";
  html += "</div>";
  html += "</details>";
  html += "</div>";
  html += "<div class='panel'>";
  html += "<details class='acc'>";
  html += "<summary>OSC Settings</summary>";
  html += "<div class='single'>";
  html += "<label><input type='checkbox' name='osc_enable' value='1'";
  html += oscEnabled ? " checked" : "";
  html += "> Enable OSC output</label>";
  html += "</div>";
  html += "<div class='single'>";
  html += "<label>OSC IP</label>";
  html += "<input name='osc_ip' value='";
  html += htmlEscape(oscIp);
  html += "'>";
  html += "</div>";
  html += "<div class='single'>";
  html += "<label>OSC Port</label>";
  html += "<input name='osc_port' value='";
  html += oscPort;
  html += "'>";
  html += "</div>";
  html += "<div class='single'>";
  html += "<label>FULL Address</label>";
  html += "<input name='osc_address_full' value='";
  html += htmlEscape(oscAddressFull);
  html += "'>";
  html += "</div>";
  html += "<div class='single'>";
  html += "<label>FULL Value (int)</label>";
  html += "<input name='osc_value_full' value='";
  html += oscValueFull;
  html += "'>";
  html += "</div>";
  html += "<div class='single'>";
  html += "<label>MISSING Address</label>";
  html += "<input name='osc_address_missing' value='";
  html += htmlEscape(oscAddressMissing);
  html += "'>";
  html += "</div>";
  html += "<div class='single'>";
  html += "<label>MISSING Value (int)</label>";
  html += "<input name='osc_value_missing' value='";
  html += oscValueMissing;
  html += "'>";
  html += "</div>";
  html += "<div class='note'>Set different OSC addresses and values for FULL and MISSING states.</div>";
  html += "</details>";
  html += "</div>";
  html += "<div class='panel'>";
  html += "<details class='acc'>";
  html += "<summary>Relay Reset (khi sensor OFFLINE)</summary>";
  html += "<div class='row'>";
  for (int p = 0; p < RELAY_PIN_COUNT; p++) {
    html += "<div class='field'>";
    html += "<label><input type='checkbox' name='relay_p";
    html += p;
    html += "' value='1'";
    html += relayPinEnabled[p] ? " checked" : "";
    html += "> GPIO ";
    html += relayPins[p];
    html += "</label>";
    html += "</div>";
  }
  html += "</div>";
  html += "<div class='single'>";
  html += "<label><input type='checkbox' name='relay_active_high' value='1'";
  html += relayActiveHigh ? " checked" : "";
  html += "> Kích mức HIGH (bỏ tick = kích mức LOW)</label>";
  html += "</div>";
  html += "<div class='single'>";
  html += "<label>Thời gian giữ xung (ms)</label>";
  html += "<input name='relay_ms' value='";
  html += relayPulseMs;
  html += "'>";
  html += "</div>";
  html += "<div class='note'>Khi 1 sensor chuyển sang OFFLINE, các chân đã tick được kích đúng thời gian này rồi tự nhả (tick nhiều chân để gộp dòng); không tick chân nào = tắt, giới hạn 200-30000ms.</div>";
  html += "</details>";
  html += "</div>";
  html += "<div class='panel'>";
  html += "<h3>Admin Auth</h3>";
  html += "<div class='single'>";
  html += "<label>Username</label>";
  html += "<input name='auth_user' value='";
  html += htmlEscape(authUser);
  html += "'>";
  html += "</div>";
  html += "<div class='single'>";
  html += "<label>Password (leave blank to keep current)</label>";
  html += "<input type='password' name='auth_pass' value=''>";
  html += "</div>";
  html += "<div class='note'>Required (HTTP Basic Auth) to Save Settings or use the Test buttons below. Change from the shipped default as soon as possible (F6).</div>";
  html += "</div>";
  html += "</div>"; // end tab-general
  html += "<div id='tab-network' class='tab-content'>";
  html += "<div class='panel'>";
  html += "<h3>Ethernet Static IP (fallback)</h3>";
  html += "<div class='single'>";
  html += "<label><input type='checkbox' name='eth_static_first' value='1'";
  html += ethUseStaticFirst ? " checked" : "";
  html += "> Ưu tiên IP tĩnh (bỏ qua DHCP)</label>";
  html += "</div>";
  html += "<div class='single'>";
  html += "<label>Static IP</label>";
  html += "<input name='eth_ip' value='";
  html += htmlEscape(ethStaticIp);
  html += "'>";
  html += "</div>";
  html += "<div class='single'>";
  html += "<label>Gateway</label>";
  html += "<input name='eth_gw' value='";
  html += htmlEscape(ethStaticGateway);
  html += "'>";
  html += "</div>";
  html += "<div class='single'>";
  html += "<label>Netmask</label>";
  html += "<input name='eth_mask' value='";
  html += htmlEscape(ethStaticNetmask);
  html += "'>";
  html += "</div>";
  html += "<div class='note'>Mặc định thử DHCP 10s rồi mới dùng IP tĩnh; sau khi áp IP tĩnh board ping gateway để xác minh, sai mạng (hoặc router chặn ICMP) thì lùi về DHCP. Đổi ở đây phải reboot mới áp dụng.</div>";
  html += "</div>";
  html += "</div>"; // end tab-network
  html += "<input class='btn' type='submit' value='SAVE SETTINGS'>";
  html += "</form>";
  html += "<div class='panel'>";
  html += "<h3>Test Settings</h3>";
  // Mot nut duy nhat: truoc day co "Test MQTT" va "Test OSC" rieng nhung ca hai deu goi
  // triggerFull(), von ban CA 2 kenh - sendOscState() nam NGOAI khoi if(mqttEnabled). Hai nut
  // gay hieu nham la test duoc tung kenh mot: dang soi "MQTT khong toi noi" ma bam Test MQTT
  // roi thay ben nhan OSC phan hoi thi rat de ket luan nham la MQTT on.
  html += "<form action='/test_iot' method='POST' style='margin-bottom:10px;'>";
  html += "<input class='btn' type='submit' value='Test MQTT + OSC (FULL)'>";
  html += "</form>";
  html += "<div class='note'>Bắn trạng thái FULL ra cả MQTT lẫn OSC cùng lúc - không tách riêng từng kênh được.</div>";
  html += "<form action='/test_relay' method='POST' style='margin-top:10px;'>";
  html += "<input class='btn' type='submit' value='Test Relay'>";
  html += "</form>";
  html += "</div>";
  html += "<div class='panel'>";
  html += "<h3>Firmware Update (OTA)</h3>";
  html += "<div class='note'>Chọn firmware.bin rồi Upload, board tự khởi động lại; KHÔNG rút nguồn/mất mạng giữa chừng - hỏng thì phải nạp lại qua USB.</div>";
  html += "<form action='/update' method='POST' enctype='multipart/form-data' onsubmit=\"return confirm('Nạp firmware mới? Board sẽ khởi động lại sau khi xong.');\">";
  html += "<input type='file' name='firmware' accept='.bin' required style='width:100%;padding:10px;border:1px solid #bfc9d6;border-radius:8px;margin-bottom:8px;background:#fff'>";
  html += "<input class='btn' type='submit' value='Upload &amp; Update'>";
  html += "</form>";
  html += "<div class='note' style='margin-top:14px'><b>Hoặc nạp từ link:</b> board tự tải firmware.bin về từ URL đã lưu - tiện khi nạp nhiều board giống nhau. Chỉ hỗ trợ <code>http://</code>, đặt file trên máy trong mạng LAN (ví dụ <code>python -m http.server 8000</code>).</div>";
  // 2 nut cung form, phan biet bang name='act': dung <button> chu khong <input type=submit> vi
  // <input> lay chinh nhan hien thi lam gia tri gui di, tuc nhan nut se phai la "update"/"save".
  html += "<form action='/update_url' method='POST'>";
  html += "<input name='ota_url' placeholder='http://192.168.99.187:8000/firmware.bin' value='";
  html += htmlEscape(otaUrl);
  html += "' style='width:100%;padding:10px;border:1px solid #bfc9d6;border-radius:8px;margin-bottom:8px;background:#fff'>";
  html += "<button class='btn' type='submit' name='act' value='save' style='margin-top:0'>Lưu URL</button>";
  html += "<button class='btn' type='submit' name='act' value='update' onclick=\"return confirm('Tải firmware từ link và nạp? Board sẽ khởi động lại sau khi xong.');\">Nạp từ link</button>";
  html += "</form>";
  html += "</div>";
  //================ LOG ================
  // Doc bang textContent chu khong phai innerHTML: log chua ten SSID / URL do nguoi khac dat,
  // nhet thang vao innerHTML la mo duong cho the <script> trong mot cai ten AP chay tren trang
  // nay. textContent hien nguyen van, khong dien giai gi.
  html += "<div class='panel'>";
  html += "<h3>Log</h3>";
  html += "<div class='note'>Nhật ký khởi động và sự kiện mạng, đọc từ RAM của board (60 dòng gần nhất, mất khi cúp điện). Số đầu dòng là giây kể từ lúc board khởi động.</div>";
  html += "<div class='row'>";
  html += "<button class='btn btn-test' style='width:auto;flex:0 0 130px' type='button' onclick='loadLog()'>Tải lại log</button>";
  html += "</div>";
  html += "<pre id='logbox' style='background:#0f172a;color:#e2e8f0;padding:10px;border-radius:8px;"
          "font-size:11px;line-height:1.45;max-height:320px;overflow:auto;white-space:pre-wrap;"
          "word-break:break-word;margin-top:8px'>Bấm \"Tải lại log\"...</pre>";
  html += "</div>";

  html += "<div class='panel'>";
  html += "<h3>Khởi động lại</h3>";
  html += "<div class='note'>Reset mềm board này thôi (node cảm biến vệ tinh KHÔNG reset theo), cấu hình đã lưu không mất, mất ~15-20 giây để lên mạng lại rồi F5.</div>";
  html += "<form action='/reboot' method='POST' onsubmit=\"return confirm('Khởi động lại board? Cảm biến và relay ngưng vài chục giây - đừng bấm khi khách đang chơi.');\">";
  html += "<input class='btn' type='submit' value='⟳ RESET ESP32'>";
  html += "</form>";
  html += "</div>";
  html += "</div>";
  html += "</body>";
  html += "</html>";
  server.send(200, "text/html", html);
}
