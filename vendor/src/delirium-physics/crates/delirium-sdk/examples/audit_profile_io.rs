//! Count real Write calls for a recorded profiler row, without the game.
use std::io::Write;
#[derive(Default)] struct Sink { calls: usize, bytes: Vec<u8> }
impl Write for Sink {
    fn write(&mut self,b:&[u8])->std::io::Result<usize>{self.calls+=1;self.bytes.extend_from_slice(b);Ok(b.len())}
    fn flush(&mut self)->std::io::Result<()>{Ok(())}
}
fn main(){
    let path=std::env::args().nth(1).expect("recorded profile JSONL");
    let source=std::fs::read_to_string(path).unwrap();
    let value:serde_json::Value=serde_json::from_str(source.lines().next().unwrap()).unwrap();
    let(mut old,mut batched)=(Sink::default(),Sink::default());
    writeln!(old,"{value}").unwrap();
    let line=value.to_string();writeln!(batched,"{line}").unwrap();
    assert_eq!(old.bytes,batched.bytes);
    println!("{{\"same_bytes\":true,\"bytes\":{},\"direct_display_writes\":{},\"batched_writes\":{}}}",old.bytes.len(),old.calls,batched.calls);
}
