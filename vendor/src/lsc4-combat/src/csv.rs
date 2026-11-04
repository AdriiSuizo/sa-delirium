//! The game's CSV reader (Xbox 360 exe, TU8), which action_table.csv is read through (board rows C1fg1, C1fg). Its
//! object: the stream (+0), the file's length (+4, 0x821B53D8), where the stream is after the last line read (+8,
//! 0x821B53E0), that line (+0x0C, 1024 bytes, read with 0x821B5240), whether a line was read (+0x40C), its length
//! (+0x410), the place in it (+0x414), the character there (+0x418), the one before (+0x419) and the delimiter
//! (+0x41A). Opened by 0x826C0948.
//!
//! The file is handed out a character at a time (0x826C0A68): each line's bytes, then its zero; the next character
//! after the zero reads the next line. It is at its end (0x826C09D8) when a line was read and (place - length) +
//! position >= the file's length: so at the zero of the file's last line, or after a read found no more lines.

/// The reader (0x826C0948's object).
#[derive(Clone)]
pub struct CsvReader<'a> {
    file: &'a [u8],
    at: usize,
    position: i32,
    line: [u8; 1024],
    read: bool,
    len: i32,
    place: i32,
    current: u8,
    previous: u8,
    delimiter: u8,
}

impl<'a> CsvReader<'a> {
    /// 0x826C0948: `file` opened with `delimiter`.
    pub fn new(file: &'a [u8], delimiter: u8) -> Self {
        CsvReader { file, at: 0, position: 0, line: [0; 1024], read: false, len: 0, place: 0, current: 0, previous: 0, delimiter }
    }

    /// 0x826C0A10: back to the start (the stream rewound, 0x821B5238).
    pub fn rewind(&mut self) {
        let (file, delimiter) = (self.file, self.delimiter);
        *self = CsvReader::new(file, delimiter);
    }

    /// 0x826C09D8.
    pub fn at_end(&self) -> bool {
        self.read && (self.place - self.len).wrapping_add(self.position) >= self.file.len() as i32
    }

    /// The character the reader is on (+0x418) and the one before (+0x419).
    pub fn current(&self) -> u8 {
        self.current
    }
    pub fn previous(&self) -> u8 {
        self.previous
    }

    /// 0x826C0A68: the next character.
    pub fn next_char(&mut self) -> u8 {
        self.previous = self.current;
        if self.read && self.place < self.len {
            self.place += 1;
        } else {
            let _ = crate::textlines::read_line(&mut self.line, self.file, &mut self.at);
            self.position = self.at as i32;
            self.read = true;
            self.len = self.line.iter().position(|&c| c == 0).unwrap_or(self.line.len()) as i32;
            self.place = 0;
        }
        self.current = self.line[self.place as usize];
        self.current
    }

    /// 0x826C0B40: on to the end of the line (true), unless the reader is already on a line's zero (true at once);
    /// false when the file ends first.
    pub fn skip_line(&mut self) -> bool {
        if self.current == 0 {
            return true;
        }
        while !self.at_end() {
            if self.next_char() == 0 {
                return true;
            }
        }
        false
    }

    /// 0x826C0BD0: past `n` line ends; true when the file has not ended.
    pub fn skip_lines(&mut self, mut n: i32) -> bool {
        while !self.at_end() && n != 0 {
            if self.next_char() == 0 {
                n -= 1;
            }
        }
        !self.at_end()
    }

    /// 0x826C0C88 (`quotes` false) and 0x826C0D38 (true): the characters up to the delimiter or the line's end
    /// (`\r` left out; with `quotes`, a `"` turns quoting on and off and is left out, and the delimiter is kept while
    /// quoting). None when `size` characters are read before it ends (the game then zeroes the last of them and
    /// returns false), with what was read, `size` - 1 bytes.
    pub fn field(&mut self, size: usize, quotes: bool) -> (bool, Vec<u8>) {
        let mut out = vec![];
        let mut quoted = false;
        while !self.at_end() {
            let c = self.next_char();
            if quotes && c == b'"' {
                quoted = !quoted;
                continue;
            }
            if c == b'\r' {
                continue;
            }
            if c == 0 || (!quoted && c == self.delimiter) {
                break;
            }
            out.push(c);
            if out.len() == size {
                out.pop();
                return (false, out);
            }
        }
        (true, out)
    }

    /// 0x826C0E10: a 32-byte field read with atoi (0x82A00B48); None when the field overflows.
    pub fn int(&mut self) -> Option<i32> {
        let (ok, f) = self.field(32, false);
        ok.then(|| crate::textfmt::atoi(&f))
    }

    /// 0x826C0E80: a 32-byte field read with atof (0x82A03578), rounded to a float.
    pub fn float(&mut self) -> Option<f32> {
        let (ok, f) = self.field(32, false);
        ok.then(|| atof(&f) as f32)
    }

    /// 0x826C0EF0: a 64-byte field read with " %f : %f " into `lo` and `hi`, both set to 0 first; true when both are
    /// read.
    pub fn range(&mut self) -> (bool, f32, f32) {
        let (ok, f) = self.field(64, false);
        if !ok {
            return (false, 0.0, 0.0);
        }
        let s = crate::crt::sscanf(&f, b" %f : %f ");
        (s.count == 2, s.float(0).unwrap_or(0.0), s.float(1).unwrap_or(0.0))
    }
}

/// atof (0x82A03578): the C runtime's conversion to a double (as sscanf's `%lf` does it, `crt`), 0 when there is no
/// number.
pub fn atof(text: &[u8]) -> f64 {
    let s = crate::crt::sscanf(text, b"%lf");
    match s.values.first() {
        Some(crate::crt::Value::Double(v)) => *v,
        _ => 0.0,
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn fields() {
        let file = b"Name,1, 2.5 ,\"a,b\", 1 : 3\nnext";
        let mut r = CsvReader::new(file, b',');
        assert_eq!(r.field(64, false), (true, b"Name".to_vec()));
        assert_eq!(r.int(), Some(1));
        assert_eq!(r.float(), Some(2.5));
        assert_eq!(r.field(64, true), (true, b"a,b".to_vec()));
        assert_eq!(r.range(), (true, 1.0, 3.0));
        assert!(r.skip_line());
        assert_eq!(r.field(3, false), (false, b"ne".to_vec()));
    }
}
