local P = {}

-- モジュールについての情報を返す
-- @return {name=モジュール名, description=モジュールの説明}
function P.get_info()
  return {
    name = i18n({
      ja_JP = [=[テキスト]=],
      en_US = [=[Text]=],
      zh_CN = [=[文本]=],
    }),
    description = i18n({
      ja_JP = [=[テキストオブジェクトで字幕を表示します。]=],
      en_US = [=[Displays subtitles with text objects.]=],
      zh_CN = [=[使用文本对象显示字幕。]=],
    }),
  }
end

local exo = {}
local fileinfo = nil
local num_objs = 0

local function add_item(layer, st, ed, text)
  table.insert(
    exo,
    string.format(
      "[%d]\r\n"
        .. "start=%d\r\n"
        .. "end=%d\r\n"
        .. "layer=%d\r\n"
        .. "group=1\r\n"
        .. "overlay=1\r\n"
        .. "camera=0\r\n",
      num_objs,
      st,
      ed,
      layer
    )
  )
  table.insert(
    exo,
    string.format(
      "[%d.0]\r\n"
        .. "_name=テキスト\r\n"
        .. "サイズ=34\r\n"
        .. "表示速度=0.0\r\n"
        .. "文字毎に個別オブジェクト=0\r\n"
        .. "移動座標上に表示する=0\r\n"
        .. "自動スクロール=0\r\n"
        .. "B=0\r\n"
        .. "I=0\r\n"
        .. "type=0\r\n"
        .. "autoadjust=0\r\n"
        .. "soft=1\r\n"
        .. "monospace=0\r\n"
        .. "align=4\r\n"
        .. "spacing_x=0\r\n"
        .. "spacing_y=0\r\n"
        .. "precision=1\r\n"
        .. "color=ffffff\r\n"
        .. "color2=000000\r\n"
        .. "font=MS UI Gothic\r\n"
        .. "text=",
      num_objs
    )
  )
  table.insert(exo, exotext(text))
  table.insert(
    exo,
    string.format(
      "\r\n"
        .. "[%d.1]\r\n"
        .. "_name=標準描画\r\n"
        .. "X=0.0\r\n"
        .. "Y=0.0\r\n"
        .. "Z=0.0\r\n"
        .. "拡大率=100.00\r\n"
        .. "透明度=0.0\r\n"
        .. "回転=0.00\r\n"
        .. "blend=0\r\n",
      num_objs
    )
  )
  num_objs = num_objs + 1
end

-- 変換処理の最初に呼ばれる
-- @param fi ファイル情報
-- @return 中断したいときは false を返す
function P.on_start(fi)
  -- fi = {
  --   width = 1920,
  --   height = 1080,
  --   rate = 60000,
  --   scale = 1000,
  --   length = 1000,
  --   audio_rate = 48000,
  --   audio_ch = 2,
  -- }
  table.insert(
    exo,
    string.format(
      "[exedit]\r\n"
        .. "width=%d\r\n"
        .. "height=%d\r\n"
        .. "rate=%d\r\n"
        .. "scale=%d\r\n"
        .. "length=%d\r\n"
        .. "audio_rate=%d\r\n"
        .. "audio_ch=%d\r\n",
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

-- 文章の塊ごとに呼ばれる
-- @param seg 文章の情報
-- @return 中断したいときは false を返す
function P.on_segment(seg)
  -- seg = {
  --   start = 1.23,
  --   end = 4.56,
  --   text = "Hello!",
  --   words = {
  --     {start = 1.23, end = 2.34, word = "He"},
  --     {start = 2.34, end = 3.0, word = "llo!"},
  --   },
  -- }
  add_item(
    1,
    math.floor(seg.start * fileinfo.rate / fileinfo.scale) + 1,
    math.floor(seg["end"] * fileinfo.rate / fileinfo.scale),
    seg.text
  )
  debug_print(string.format("%7.2fs - %7.2fs %s", seg.start, seg["end"], seg.text))
  return true
end

-- 変換処理の最後に呼ばれる
-- ここでUTF8でエンコードされたEXOファイルの内容を返すと、最終的に Shift_JIS に変換されて読み込まれる
-- @return EXO ファイルの内容
function P.on_finalize()
  return table.concat(exo)
end

return P
