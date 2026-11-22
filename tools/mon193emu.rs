//! The emulator's end of tools/bench193.py: an emulated 193 whose CAN
//! bus is this process's standard input and output, in the CANable's
//! slcan text (`t7F2 8 ...`), so that the same script runs against it
//! as against a unit on the bench.
//!
//!     cargo run --release --example mon193emu -- --disc LOADER.iso [--variant V575] [--index 0310]
//!     cargo run --release --example mon193emu -- --direct out/mon193-193.bin
//!
//! With `--disc` the unit has the disc's software installed under the
//! index given (lower than the disc's: the stock software then starts
//! the disc's loader, as on the bench); with `--direct` a program is
//! started as the SD loader starts one (and `--disc` with it puts that
//! disc in the drive).  A run that ends (the watchdog,
//! a restart) starts the unit again, as a unit does.  Emulated time is
//! held back to the clock's, never ahead of it.  `--strict` models the
//! CPU's caches as the kit's tests do (several times slower).
use rnse::board::Model;
use rnse::dev::hcan::Frame;
use rnse::fw;
use rnse::machine::Machine;
use rnse::sh4::cpu::Stop;
use std::io::{BufRead, Write};

fn boot(disc: &Option<Vec<u8>>, direct: &Option<Vec<u8>>, variant: &str, index: &str, strict: bool) -> Machine {
    let mut m = Machine::for_model(Model::Rnse193);
    m.echo_console = false;
    m.scramble_ram();
    if strict {
        m.cache_model();
    }
    m.install_boot_stub(variant);
    if let Some(program) = direct {
        m.board.load(0x09b6_0080, program);
        m.cpu.reset();
        m.cpu.set_sr(0x7000_00f0);
        m.cpu.pc = u32::from_be_bytes([program[0], program[1], program[2], program[3]]);
        m.cpu.r[15] = 0x09c5_fff0;
        // As the SD loader leaves them: the watchdog armed, the CAN
        // controller running.
        m.watchdog_always = true;
        m.can().start();
        // With --disc as well: that disc in the drive.
        if let Some(image) = disc {
            m.atapi().load(image.clone());
        }
    } else {
        let image = disc.as_ref().unwrap();
        let variants = fw::load_variants(image).expect("the disc's software");
        let v = variants.iter().find(|v| v.name == variant).expect("the variant on the disc");
        let mut idx = fw::IndexInfo::from_disc(image, &v.name).unwrap_or_default();
        idx.sofidx = index.to_string();
        m.install(v, &idx);
        m.atapi().load(image.clone());
        m.start_os(v).expect("the stock software's start");
        m.set_ignition(true);
    }
    // MON193EMU_TRACE=1: the controller's frames on standard error.
    m.can().trace = std::env::var_os("MON193EMU_TRACE").is_some();
    m
}

fn main() {
    let args: Vec<String> = std::env::args().skip(1).collect();
    let (mut disc, mut direct, mut variant, mut index) = (None, None, "V575".to_string(), "0310".to_string());
    let mut strict = false;
    let mut i = 0;
    while i < args.len() {
        let v = args.get(i + 1).cloned().unwrap_or_default();
        match args[i].as_str() {
            "--strict" => {
                // The CPU's caches modelled (several times slower).
                strict = true;
                i += 1;
                continue;
            }
            "--disc" => disc = Some(std::fs::read(&v).expect("--disc")),
            "--direct" => direct = Some(std::fs::read(&v).expect("--direct")),
            "--variant" => variant = v,
            "--index" => index = v,
            o => panic!("unknown option {}", o),
        }
        i += 2;
    }
    assert!(disc.is_some() || direct.is_some(), "--disc FILE or --direct FILE");
    let (tx, rx) = std::sync::mpsc::channel::<String>();
    std::thread::spawn(move || {
        let stdin = std::io::stdin();
        let mut line = Vec::new();
        let mut input = stdin.lock();
        loop {
            line.clear();
            match input.read_until(b'\r', &mut line) {
                Ok(0) | Err(_) => break,
                Ok(_) => {
                    let _ = tx.send(String::from_utf8_lossy(&line).trim().to_string());
                }
            }
        }
        let _ = tx.send("quit".into());
    });
    let mut m = boot(&disc, &direct, &variant, &index, strict);
    let ms = m.cycles_per_ms();
    let began = std::time::Instant::now();
    let mut emulated = 0u64;
    let out = std::io::stdout();
    loop {
        loop {
            match rx.try_recv() {
                Ok(line) if line == "quit" => return,
                Ok(line) => {
                    let b = line.as_bytes();
                    if b.first() == Some(&b'V') {
                        let mut o = out.lock();
                        let _ = o.write_all(b"emulator\r");
                        let _ = o.flush();
                    } else if b.first() == Some(&b't') && b.len() >= 5 {
                        let id = u16::from_str_radix(&line[1..4], 16).unwrap_or(0);
                        let n = (b[4] - b'0') as usize;
                        let data: Vec<u8> = (0..n).filter_map(|k| line.get(5 + 2 * k..7 + 2 * k).and_then(|h| u8::from_str_radix(h, 16).ok())).collect();
                        m.can_frame(Frame::new(id, &data));
                    }
                }
                Err(_) => break,
            }
        }
        let stop = m.run(ms);
        emulated += 1;
        let sent: Vec<Frame> = m.can().tx.drain(..).collect();
        if !sent.is_empty() {
            let mut o = out.lock();
            for f in sent {
                let hex: String = f.data[..f.len as usize].iter().map(|b| format!("{:02X}", b)).collect();
                let _ = write!(o, "t{:03X}{}{}\r", f.id, f.len, hex);
            }
            let _ = o.flush();
        }
        if stop != Stop::Budget {
            eprintln!("[emulator] the run ended at {} ms ({:?}, {:?}): the unit starts again", emulated, stop, m.cpu.fault);
            m = boot(&disc, &direct, &variant, &index, strict);
        }
        let ahead = (emulated as u128).saturating_sub(began.elapsed().as_millis());
        if ahead > 5 {
            std::thread::sleep(std::time::Duration::from_millis(ahead as u64));
        }
    }
}
