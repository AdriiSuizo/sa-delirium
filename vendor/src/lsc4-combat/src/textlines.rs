//! The game's text-file line reader (Xbox 360 exe, Title Update 8), which most of its data files are read through:
//! 0x821B52F0 over 0x821B5240 over the stream's 0x8285B490 and 0x8285B3F0 (board row C6b2b, for hudcolor.dat).
//!
//! - 0x8285B3F0 reads up to and with a `\n`, at most 1022 bytes (0x821B5240 hands it 1023 of its 1024), into the
//!   buffer from its start, and puts a zero after them.
//! - 0x8285B490 then drops the line's last bytes while they are at most a space as a signed byte (so also every byte
//!   from 0x80 up), a zero where the kept ones end.
//! - 0x821B5240: nothing left, with the stream at its end: no more lines. Each `\r` up to the line's end (or a zero
//!   in it) becomes a zero, so the line ends at its first zero or `\r`; a zero where the kept bytes end.
//! - 0x821B52F0, with `blank` (its second argument): each byte below 0x20 (as a signed byte: not 0x80 and up) and
//!   each comma up to the line's zero becomes a space. Then leading spaces are skipped: the line is handed out as a
//!   pointer into the buffer.
//!
//! The buffer is one for the whole game (1024 bytes at 0x82B2F3A8, zero at start), so after a line's zero it still
//! holds the bytes this read dropped (its `\r\n` and trailing spaces) and, past them, what longer lines before it
//! left, from any file. That matters to the readers that take a line's fields with `sscanf`: once a format asks for
//! more fields than the line has, the C runtime reads on past the line's zero into the buffer (`crt`).

use std::cell::RefCell;

/// The game's line buffer (0x82B2F3A8).
#[derive(Clone)]
pub struct LineBuffer {
    pub bytes: [u8; 1024],
}

impl Default for LineBuffer {
    fn default() -> Self {
        LineBuffer { bytes: [0; 1024] }
    }
}

impl LineBuffer {
    /// 0x821B52F0: the next line of `file` from `*at` (moved on past it), `blank` as its second argument. Gives
    /// where in the buffer the line starts; None when the file has no more lines.
    pub fn read(&mut self, file: &[u8], at: &mut usize, blank: bool) -> Option<usize> {
        read_line(&mut self.bytes, file, at)?;
        let b = &mut self.bytes;
        // 0x821B52F0.
        if blank {
            let mut j = 0;
            while b[j] != 0 {
                if (b[j] < b' ' && (b[j] as i8) >= 0) || b[j] == b',' {
                    b[j] = b' ';
                }
                j += 1;
            }
        }
        let mut start = 0;
        while b[start] == b' ' {
            start += 1;
        }
        Some(start)
    }

    /// The line that starts at `start`, up to its zero.
    pub fn text(&self, start: usize) -> &[u8] {
        let m = &self.bytes[start..];
        &m[..m.iter().position(|&c| c == 0).unwrap_or(m.len())]
    }

    /// The buffer from `start` on: the line, its zero and what the buffer holds after it.
    pub fn memory(&self, start: usize) -> &[u8] {
        &self.bytes[start..]
    }
}

/// 0x821B5240 into `b` (its buffer, of `b.len()` bytes): the next line of `file` from `*at` (moved on past it), up
/// to its first zero or `\r`, a zero after it; false when the file has no more lines. 0x8285B3F0 takes at most
/// `b.len()` - 2 bytes, stopping after a `\n`, then 0x8285B490 drops the trailing bytes up to a space (signed).
pub fn read_line(b: &mut [u8], file: &[u8], at: &mut usize) -> Option<()> {
    let rest = &file[(*at).min(file.len())..];
    let limit = b.len() - 2;
    let take = rest.iter().take(limit).position(|&c| c == b'\n').map_or(rest.len().min(limit), |i| i + 1);
    b[..take].copy_from_slice(&rest[..take]);
    b[take] = 0;
    *at += take;
    let mut n = take;
    while n != 0 && (b[n - 1] as i8) <= 0x20 {
        n -= 1;
    }
    b[n] = 0;
    if n == 0 && *at >= file.len() {
        return None;
    }
    let mut i = 0;
    let mut zero = false;
    while i < n {
        match b[i] {
            0 => {
                zero = true;
                break;
            }
            b'\n' => break,
            b'\r' => b[i] = 0,
            _ => {}
        }
        i += 1;
    }
    if !zero {
        b[i] = 0;
    }
    Some(())
}

thread_local! {
    /// The buffer every reader here goes through, as the game's one. ponytail: one a thread; the game loads its
    /// files on one, so this is the game's buffer when ours are read on one thread in the game's order.
    static SHARED: RefCell<LineBuffer> = RefCell::new(LineBuffer::default());
}

/// Each line of `file` as 0x821B52F0 hands it out (`blank` its second argument), through the shared buffer: `f` gets
/// the buffer from the line's start (the line up to its zero, then what the buffer holds after it).
pub fn each_line(file: &[u8], blank: bool, mut f: impl FnMut(&[u8])) {
    let mut at = 0;
    loop {
        // The buffer is lent out for one line at a time, so `f` may read other files.
        let line = SHARED.with(|s| {
            let mut s = s.borrow_mut();
            s.read(file, &mut at, blank).map(|start| s.memory(start).to_vec())
        });
        let Some(line) = line else { break };
        f(&line);
    }
}

/// The lines of a file as 0x821B52F0 hands them out, `blank` as its second argument (through the shared buffer).
pub fn lines(file: &[u8], blank: bool) -> Vec<Vec<u8>> {
    let mut out = vec![];
    each_line(file, blank, |m| out.push(m[..m.iter().position(|&c| c == 0).unwrap_or(m.len())].to_vec()));
    out
}

#[cfg(test)]
mod tests {
    #[test]
    fn lines_as_the_game_reads_them() {
        let l = super::lines(b"  a,b\tc\r\n\nx\x01y \xe9\r\nlast  ", true);
        assert_eq!(l, vec![b"a b c".to_vec(), vec![], b"x y".to_vec(), b"last".to_vec()]);
        assert_eq!(super::lines(b"a,b\n", false), vec![b"a,b".to_vec()]);
        // A blank last line at the end of the file is no line.
        assert_eq!(super::lines(b"a\n   ", true), vec![b"a".to_vec()]);
        let long = vec![b'z'; 1500];
        assert_eq!(super::lines(&long, true).iter().map(Vec::len).collect::<Vec<_>>(), vec![1022, 478]);
    }

    #[test]
    fn what_a_line_leaves_in_the_buffer() {
        let mut b = super::LineBuffer::default();
        let mut at = 0;
        let file = b"name, a, 1, 2, 3\r\nx y\r\n";
        let s = b.read(file, &mut at, true).unwrap();
        assert_eq!(b.text(s), b"name  a  1  2  3");
        // The second line's zero, its dropped "\n" and zero, then the first line's tail.
        let s = b.read(file, &mut at, true).unwrap();
        assert_eq!(&b.memory(s)[..12], b"x y\0\n\0a  1  ");
    }
}
