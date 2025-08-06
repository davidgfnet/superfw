
unscii-16.hex and unscii-16-full.hex

Downloaded from http://viznut.fi/unscii/ under the GPL license

hangul-blocks.hex is also part of unifont, manually filtered and
re-numbered to work with hangul.h

The font pack has been generated using:

./generator.py  --font-files unscii-16-full.hex hangul-blocks.hex \
  --font-blocks cjk-sym,latin,latin-a,latin-b,greek,cyrilic,hiragana,katakana,cjk-uni,hangul-part \
  --output ../fonts.pack

