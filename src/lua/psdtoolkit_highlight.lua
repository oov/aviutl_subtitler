local P = {}

function P.get_info()
  return {
    name = i18n({
      ja_JP = [=[字幕準備ハイライト(PSDToolKit)]=],
      en_US = [=[Subtitle Preparation Highlight(PSDToolKit)]=],
      zh_CN = [=[字幕准备高亮(PSDToolKit)]=],
    }),
    description = i18n({
      ja_JP = [=[PSDToolKitで使える字幕準備オブジェクトを配置します。]=],
      en_US = [=[Places subtitle preparation objects usable with PSDToolKit.]=],
      zh_CN = [=[放置可与PSDToolKit一起使用的字幕准备对象。]=],
    }),
  }
end

local exo = {}
local fileinfo = nil
local num_objs = 0;

local function add_item(layer, st, ed, text)
  table.insert(
    exo,
    string.format(
      "[%d]\r\n"..
      "start=%d\r\n"..
      "end=%d\r\n"..
      "layer=%d\r\n"..
      "group=1\r\n"..
      "overlay=1\r\n"..
      "camera=0\r\n",
      num_objs,
      st,
      ed,
      layer
    )
  )
  table.insert(
    exo,
    string.format(
      "[%d.0]\r\n"..
      "_name=テキスト\r\n"..
      "サイズ=1\r\n"..
      "表示速度=0.0\r\n"..
      "文字毎に個別オブジェクト=0\r\n"..
      "移動座標上に表示する=0\r\n"..
      "自動スクロール=0\r\n"..
      "B=0\r\n"..
      "I=0\r\n"..
      "type=0\r\n"..
      "autoadjust=0\r\n"..
      "soft=1\r\n"..
      "monospace=0\r\n"..
      "align=4\r\n"..
      "spacing_x=0\r\n"..
      "spacing_y=0\r\n"..
      "precision=1\r\n"..
      "color=ffffff\r\n"..
      "color2=000000\r\n"..
      "font=MS UI Gothic\r\n"..
      "text=",
      num_objs
    )
  )
  table.insert(exo, exotext(text))
  table.insert(
    exo,
    string.format(
      "\r\n"..
      "[%d.1]\r\n"..
      "_name=標準描画\r\n"..
      "X=0.0\r\n"..
      "Y=0.0\r\n"..
      "Z=0.0\r\n"..
      "拡大率=100.00\r\n"..
      "透明度=100.0\r\n"..
      "回転=0.00\r\n"..
      "blend=0\r\n",
      num_objs
    )
  )
  num_objs = num_objs + 1  
end

function P.on_start(fi)
  table.insert(
    exo,
    string.format(
      "[exedit]\r\n"..
      "width=%d\r\n"..
      "height=%d\r\n"..
      "rate=%d\r\n"..
      "scale=%d\r\n"..
      "length=%d\r\n"..
      "audio_rate=%d\r\n"..
      "audio_ch=%d\r\n",
      fi.width,
      fi.height,
      fi.rate,
      fi.scale,
      fi.length,
      fi.audio_rate,
      fi.audio_ch
    )
  )
  fileinfo = fi
  return true
end

function P.on_segment(seg)
  local segments = {}
  for i, word in ipairs(seg.words) do
    local endsec
    if i == #seg.words then
      endsec = seg["end"]
    else
      endsec = math.max(word["end"], seg.words[i+1].start)
    end
    table.insert(segments, string.format("%q", word.word))
    add_item(
      1,
      math.floor(word.start * fileinfo.rate / fileinfo.scale) + 1,
      math.floor(endsec * fileinfo.rate / fileinfo.scale),
      '<?\r\n--[[\r\ncolor2 = "<#333333,000000>"\r\n--]]sbtr={idx='..i..';set=function(self,text)require(\"PSDToolKit\").subtitle:set(table.concat(text,"",1,self.idx)..(color2 or "<#333333,000000>")..table.concat(text,"",self.idx+1),obj,false)color2=nil end}\r\n?>'
    )
  end
  add_item(
    2,
    math.floor(seg.start * fileinfo.rate / fileinfo.scale) + 1,
    math.floor(seg["end"] * fileinfo.rate / fileinfo.scale),
    "<?sbtr:set({\r\n" .. table.concat(segments, ",") .. "\r\n});sbtr=nil?>"
  )
  debug_print(string.format("%7.2fs - %7.2fs %s", seg.start, seg["end"], seg.text))
  return true
end

function P.on_finalize()
  return table.concat(exo)
end

return P
