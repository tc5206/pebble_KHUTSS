var Clay = require('pebble-clay');
var clayConfig = require('./config');
var clay = new Clay(clayConfig);

var isHoliday = false;
var specialDates = [];
var specialSaturdays = [];
var cachedText = ""; 
var currentStationIdx = 0;
var currentTimeOffset = 0;

Pebble.addEventListener('ready', function(e) { 
  fetchTrainData(); 
});

Pebble.addEventListener('webviewclosed', function(e) {
  fetchTrainData();
});

Pebble.addEventListener('appmessage', function(e) {
  if (e.payload) {
    if (e.payload.KEY_URL_INDEX !== undefined) {
      currentStationIdx = e.payload.KEY_URL_INDEX;
    }
    if (e.payload.KEY_TIME_OFFSET !== undefined) {
      currentTimeOffset = e.payload.KEY_TIME_OFFSET;
    }
  }

  if (cachedText) {
    updateAndSend(cachedText);
  } else {
    fetchTrainData();
  }
});

function fetchTrainData() {
  var settings = JSON.parse(localStorage.getItem('clay-settings') || '{}');
  var useHolidayApi = (settings.KEY_HOLIDAY_CONFIG === "1");

  if (useHolidayApi) {
    var holidayApiUrl = "https://holidays-jp.github.io/api/v1/date.json";
    var xhrH = new XMLHttpRequest();
    xhrH.open("GET", holidayApiUrl, true);
    xhrH.onload = function() {
      if (xhrH.status === 200) {
        var holidays = JSON.parse(xhrH.responseText);
        isHoliday = !!holidays[getTodayString()];
      }
      getMainTimetable();
    };
    xhrH.onerror = function() { getMainTimetable(); };
    xhrH.send();
  } else {
    isHoliday = false;
    getMainTimetable();
  }
}

function getTodayString() {
  var now = new Date();
  if (now.getHours() < 4) now.setDate(now.getDate() - 1);
  return now.getFullYear() + "-" + ("0" + (now.getMonth() + 1)).slice(-2) + "-" + ("0" + now.getDate()).slice(-2);
}

function getMainTimetable() {
  var settings = JSON.parse(localStorage.getItem('clay-settings') || '{}');
  var index = settings.KEY_URL_INDEX || '0';
  var url = settings['KEY_URL_' + index] || '';
  if (!url) return;

  var xhr = new XMLHttpRequest();
  xhr.open("GET", url, true);
  xhr.onload = function() {
    if (xhr.status === 200) {
      cachedText = xhr.responseText;
      updateAndSend(cachedText);
    }
  };
  xhr.send();
}

function updateAndSend(text) {
  var allLines = text.split('\n');
  var stationName = "No Data";
  var slots = [null, null, null];
  var totalStationCount = 0;
  
  // 1. まずファイル全体の駅数をカウント
  for (var k = 0; k < allLines.length; k++) {
    if (allLines[k].trim().indexOf('# ') === 0) {
      totalStationCount++;
    }
  }

  // 2. 指定された駅(Index)のブロックを抽出
  var stationHeadersfound = 0;
  var targetLines = [];
  for (var l = 0; l < allLines.length; l++) {
    var rawLine = allLines[l].trim();
    if (rawLine.indexOf('# ') === 0) {
      if (stationHeadersfound === currentStationIdx) {
        stationName = rawLine.replace('# ', '').trim();
      }
      stationHeadersfound++;
      continue;
    }
    
    if (stationHeadersfound === currentStationIdx + 1) {
      targetLines.push(rawLine);
    } else if (stationHeadersfound > currentStationIdx + 1) {
      // 目的の駅ブロックが終わったら、パース自体はここで抜けて効率化
      break; 
    }
  }

  var now = new Date();
  var hour = now.getHours();
  var min = now.getMinutes();
  var day = now.getDay();
  if (hour < 4) { now.setDate(now.getDate() - 1); day = now.getDay(); hour += 24; }
  var nowTotalMin = hour * 60 + min;
  var todayStr = getTodayString();

  var targetMode = "weekday";
	specialDates = [];     // 蓄積防止
  specialSaturdays = []; // 蓄積防止
  if (isHoliday || (specialDates.indexOf(todayStr) !== -1) || day === 0) targetMode = "holiday";
  else if ((specialSaturdays.indexOf(todayStr) !== -1) || day === 6) targetMode = "saturday";

  var currentSlotIdx = -1;
  var currentMode = "";
  var candidates = [[], [], []];

  for (var i = 0; i < targetLines.length; i++) {
    var line = targetLines[i];
    if (line.indexOf('//') !== -1) line = line.split('//')[0].trim();
    if (!line) continue;

    if (line.indexOf('@HOLIDAY:') === 0) {
      specialDates = specialDates.concat(line.replace('@HOLIDAY:', '').split(','));
    } else if (line.indexOf('@SATURDAY:') === 0) {
      specialSaturdays = specialSaturdays.concat(line.replace('@SATURDAY:', '').split(','));
    } else if (line.indexOf('## ') === 0) {
      currentSlotIdx = parseInt(line.replace('## ', '').trim());
    } else if (line.indexOf('### ') === 0) {
      currentMode = line.replace('### ', '').trim().toLowerCase();
    } else if (line.indexOf('- ') === 0 && currentSlotIdx >= 0 && currentSlotIdx <= 2) {
      if (currentMode !== targetMode) continue;
      var parts = line.replace('- ', '').split(',');
      if (parts.length >= 2) {
        var timePart = parts[0].trim().split(':');
        var h = parseInt(timePart[0]); if (h < 4) h += 24;
        var m = parseInt(timePart[1]);
        var tMin = h * 60 + m;

        if (tMin > nowTotalMin) {
          candidates[currentSlotIdx].push({
            time: parts[0].trim(),
            dest: parts[1] ? parts[1].trim() : "",
            line: parts[2] ? parts[2].trim() : "",
            n1: parts[3] ? parts[3].trim() : "",
            n2: parts[4] ? parts[4].trim() : "",
            absMin: tMin
          });
        }
      }
    }
  }

  for (var s = 0; s < 3; s++) {
    if (candidates[s].length > 0) {
      candidates[s].sort(function(a, b) { return a.absMin - b.absMin; });
      var idx = Math.min(currentTimeOffset, candidates[s].length - 1);
      slots[s] = candidates[s][idx];
    }
  }

  var dict = { 
    'KEY_STATION': stationName,
    'KEY_MAX_STATIONS': totalStationCount
  };
  
  var watch = Pebble.getActiveWatchInfo ? Pebble.getActiveWatchInfo() : null;
  var isRect = (watch && watch.platform !== 'emery' && watch.platform !== 'chalk');

  for (var s = 0; s < 3; s++) {
    if (slots[s]) {
      var lineId = slots[s].line;
      var n1Id = slots[s].n1;
      if (isRect) {
        if (lineId === "京浜東北") lineId = "1";
        else if (lineId === "上野東京") lineId = "2";
        else if (lineId === "湘南新宿") lineId = "3";
        else if (lineId === "埼京川越") lineId = "4";
        else lineId = lineId.substring(0, 1);
        if (n1Id === "15両") n1Id = "a";
        else if (n1Id === "10両") n1Id = "b";
				else if (n1Id === "快速") n1Id = "c";
				else if (n1Id === "通勤快速") n1Id = "d";
				else if (n1Id === "特別快速") n1Id = "e";
        else n1Id = n1Id.substring(0, 1);
      }
      dict['KEY_S' + s + '_TIME'] = slots[s].time;
      dict['KEY_S' + s + '_DEST'] = slots[s].dest;
      dict['KEY_S' + s + '_LINE'] = lineId;
      dict['KEY_S' + s + '_N1'] = n1Id;
      dict['KEY_S' + s + '_N2'] = slots[s].n2;
    } else {
      dict['KEY_S' + s + '_TIME'] = "";
    }
  }
  Pebble.sendAppMessage(dict);
}