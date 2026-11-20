//! The SDK's programs run on the emulator: end-to-end tests that need a
//! real update disc image and an SH-4 GCC.
//!
//! The firmware is not redistributable, so these only run when the
//! environment variable `RNSE_DISC` points at an ISO image of an update
//! CD, and they build the examples with build.sh, so they are skipped
//! without an SH-4 GCC on the PATH (`tools/sh4-docker` has one in a
//! container):
//!
//!     RNSE_DISC=/path/to/disc.iso cargo test --release -p rnse-sdk -- --nocapture
//!
//! A green run without both proves nothing.  Those that compare with a
//! real boot ROM need `RNSE_BOOT_ROM`, the file `rnse report --rom`
//! writes: firmware, like the disc, and not in the repository.
//!
//! The emulator is the `rnse` crate; the helpers at the top (`disc`,
//! `sdk_example`, `started_by_stock_software`, ...) are copies of those
//! in the emulator's own tests/disc.rs.

use rnse::fw;
use rnse::machine::Machine;
use rnse::sh4::cpu::Stop;
fn disc() -> Option<Vec<u8>> {
    let path = std::env::var_os("RNSE_DISC")?;
    Some(std::fs::read(path).expect("RNSE_DISC is not readable"))
}

/// A program written from scratch (examples/hello), built with an
/// SH-4 GCC if there is one, put in the loader's place on the disc and
/// started the way the boot ROM starts the loader: it brings the board
/// up, prints on the debug serial port, draws and keeps time, on both
/// units.
/// Build an SDK example for a unit, put it in the loader's place and
/// start it.  None when there is no cross compiler or the disc has no
/// software for that unit.
fn sdk_example(image: &[u8], model: rnse::board::Model, example: &str) -> Option<(Machine, String, Vec<u8>)> {
    use rnse::board::Model;
    let sdk = std::path::Path::new(env!("CARGO_MANIFEST_DIR")).to_path_buf();
    let unit = if model == Model::Rnse192 { "192" } else { "193" };
    let variants = fw::load_variants(image).unwrap();
    let v = variants.iter().find(|v| Model::for_variant(&v.name) == Some(model))?;
    // Builds of the same example for the two tests that use it must
    // not run into each other.
    static BUILD: std::sync::Mutex<()> = std::sync::Mutex::new(());
    let built = {
        let _lock = BUILD.lock().unwrap();
        std::process::Command::new("sh").arg(sdk.join("build.sh")).args([unit, example]).output()
    };
    match built {
        Ok(out) if out.status.success() => {}
        Ok(out) => {
            // No cross compiler here: nothing to test.  A compiler that
            // is there and fails (a program that no longer fits, say)
            // is a failure, not a skip.
            let text = String::from_utf8_lossy(&out.stderr);
            assert!(text.contains("gcc: not found") || text.contains("gcc: command not found"),
                "{} for the {}: the build failed: {}", example, unit, text);
            return None;
        }
        Err(_) => return None,
    }
    let program = std::fs::read(sdk.join(format!("out/{}-{}.bin", example, unit))).unwrap();
    let (custom, places) = fw::custom_loader(image, &v.name, &program).unwrap();
    assert_eq!(places.len(), 2, "{}: the loader is on the disc in the container and as a file: {:?}", v.name, places);
    let variants = fw::load_variants(&custom).unwrap();
    let v = variants.iter().find(|x| x.name == v.name).unwrap();
    let ldm = v.module("LDM").unwrap();
    assert!(ldm.checksum_ok());

    let mut m = Machine::for_model(model);
    m.echo_console = false;
    // Whatever was in memory before: not zeroes.
    m.scramble_ram();
    m.install_boot_stub(&v.name);
    m.atapi().load(custom.clone());
    m.start_loader(ldm, [0; 3]);
    // As unforgiving as the emulator gets (`--strict`).
    m.busy_start();
    m.watchdog_always = true;
    m.cache_model();
    Some((m, v.name.clone(), custom))
}

#[test]
fn custom_program_runs() {
    use rnse::board::Model;
    let Some(image) = disc() else { return };
    for model in [Model::Rnse192, Model::Rnse193] {
        let Some((mut m, name, _)) = sdk_example(&image, model, "hello") else { continue };
        let ms = m.cycles_per_ms();
        assert_eq!(m.run(3_500 * ms), Stop::Budget, "{}: {:?}", name, m.cpu.fault);
        let text = String::from_utf8_lossy(&m.console).into_owned();
        assert!(text.contains("rnse sdk: hello"), "{}: console: {:?}", name, text);
        assert!(text.contains("tick 3") && !text.contains("tick 4"), "{}: three seconds have passed: {:?}", name, text);
        assert!(!text.contains("unexpected"), "{}: {:?}", name, text);
        // The picture: a white frame on the panel's edge, colour bars
        // in the top third.
        let (w, h, rgb) = m.render();
        let at = |x: usize, y: usize| [rgb[(y * w + x) * 3], rgb[(y * w + x) * 3 + 1], rgb[(y * w + x) * 3 + 2]];
        let near = |p: [u8; 3], q: [u8; 3]| p.iter().zip(q).all(|(a, b)| a.abs_diff(b) <= 4);
        for (x, y) in [(0, 0), (w - 1, 0), (0, h - 1), (w - 1, h - 1), (w / 2, 0), (0, h / 2), (w - 1, h / 2), (w / 2, h - 1)] {
            assert!(near(at(x, y), [255, 255, 255]), "{}: frame at {},{} is {:?}", name, x, y, at(x, y));
        }
        assert!(near(at(w * 3 / 16, h / 6), [255, 255, 0]), "{}: second bar is {:?}", name, at(w * 3 / 16, h / 6));
        assert!(near(at(w * 13 / 16, h / 6), [0, 0, 255]), "{}: seventh bar is {:?}", name, at(w * 13 / 16, h / 6));
        assert!(near(at(w / 2, h * 7 / 8), [0, 0, 0]), "{}: background is {:?}", name, at(w / 2, h * 7 / 8));
    }
}

/// The SDK's faceplate code against the faceplate controller's model:
/// the program releases the controller from reset, takes its start-up
/// frame, sends the stock settings and then follows keys and knobs.
#[test]
fn custom_program_reads_the_faceplate() {
    use rnse::board::Model;
    let Some(image) = disc() else { return };
    for model in [Model::Rnse192, Model::Rnse193] {
        let Some((mut m, name, _)) = sdk_example(&image, model, "keys") else { continue };
        let ms = m.cycles_per_ms();
        assert!(m.panel.in_reset || !m.panel.online, "{}: the controller starts in reset", name);
        assert_eq!(m.run(500 * ms), Stop::Budget, "{}: {:?}", name, m.cpu.fault);
        assert!(m.panel.online, "{}: start-up frame acknowledged", name);
        for cmd in [2, 3, 6, 7, 9, 10] {
            assert!(m.panel.from_unit[cmd].is_some(), "{}: setting {:#x} was sent", name, cmd);
        }
        assert!(m.panel_key("SETUP", true));
        m.run(100 * ms);
        assert!(m.panel_key("SETUP", false));
        assert!(m.panel_knob("main", 3));
        assert!(m.panel_knob("volume", -2));
        assert_eq!(m.run(300 * ms), Stop::Budget, "{}: {:?}", name, m.cpu.fault);
        let text = String::from_utf8_lossy(&m.console).into_owned();
        for line in ["faceplate online\r\n", "SETUP down\r\n", "SETUP up\r\n", "select 3 right\r\n", "volume 2 left\r\n"] {
            assert!(text.contains(line), "{}: {:?} in {:?}", name, line, text);
        }
        assert!(!text.contains("unexpected") && !text.contains("lost"), "{}: {:?}", name, text);
        assert_eq!(m.panel.resets, 0, "{}", name);
    }
}

/// The SDK's display driver (examples/display) against the
/// display's model: the key next to the power button opens the display
/// and closes it again, and the motor is off once it has got there,
/// stopped by the driver because the position no longer changes.  On
/// the 193 this checks the driver against the emulator's assumption of
/// which motor line closes, and nothing more.
#[test]
fn sdk_display_opens_and_closes() {
    use rnse::board::Model;
    let Some(image) = disc() else { return };
    for model in [Model::Rnse192, Model::Rnse193] {
        let Some((mut m, name, _)) = sdk_example(&image, model, "display") else { continue };
        let ms = m.cycles_per_ms();
        assert_eq!(m.run(500 * ms), Stop::Budget, "{}: {:?}", name, m.cpu.fault);
        assert!(m.display_pos() >= 894, "{}: starts closed", name);
        let press = |m: &mut Machine, key: &str| {
            assert!(m.panel_key(key, true));
            m.run(200 * ms);
            assert!(m.panel_key(key, false));
        };
        let text = |m: &Machine| String::from_utf8_lossy(&m.console).into_owned();
        press(&mut m, "EJECT");
        assert_eq!(m.display_motor(), -1, "{}: opening: {:?}", name, text(&m));
        assert_eq!(m.run(3_000 * ms), Stop::Budget, "{}: {:?}", name, m.cpu.fault);
        assert!(m.display_pos() <= 94, "{}: open: {} {:?}", name, m.display_pos(), text(&m));
        assert_eq!(m.display_motor(), 0, "{}: the motor stops when open", name);
        assert!(text(&m).contains("display open, "), "{}: {:?}", name, text(&m));
        press(&mut m, "EJECT");
        assert_eq!(m.display_motor(), 1, "{}: closing", name);
        assert_eq!(m.run(3_000 * ms), Stop::Budget, "{}: {:?}", name, m.cpu.fault);
        assert!(m.display_pos() >= 894, "{}: closed: {}", name, m.display_pos());
        assert_eq!(m.display_motor(), 0, "{}: the motor stops when closed", name);
        // Opened again and stopped half-way by SELECT: in between, not
        // stuck, and the motor off.
        press(&mut m, "NEXT");
        m.run(400 * ms);
        press(&mut m, "SELECT");
        m.run(500 * ms);
        assert_eq!(m.display_motor(), 0, "{}: stopped by SELECT", name);
        assert!(m.display_pos() > 94 && m.display_pos() < 894, "{}: in between: {}", name, m.display_pos());
        assert!(text(&m).contains("display between, "), "{}: {:?}", name, text(&m));
        // CD EJECT, behind the display, ejects the disc (panel_service).
        let ejects = m.atapi().ejects;
        press(&mut m, "CD_EJECT");
        m.run(500 * ms);
        let after = m.atapi().ejects;
        assert_eq!(after, ejects + 1, "{}: CD EJECT did not eject: {:?}", name, text(&m));
        // A short RETURN does nothing; held for two seconds it would go
        // back to the SD loader, and with none (started from a disc
        // here) the program says so and goes on, keys still answered.
        press(&mut m, "RETURN");
        m.run(500 * ms);
        assert!(!text(&m).contains("display: done"), "{}: {:?}", name, text(&m));
        assert!(m.panel_key("RETURN", true));
        m.run(2_500 * ms);
        assert!(m.panel_key("RETURN", false));
        m.run(300 * ms);
        let t = text(&m);
        assert!(t.contains("display: done") && t.contains("return unavailable"), "{}: {:?}", name, t);
        press(&mut m, "PREV");
        m.run(3_000 * ms);
        assert!(m.display_pos() >= 894, "{}: closed after all that: {}", name, m.display_pos());
        assert_eq!(m.display_motor(), 0, "{}", name);
        let t = text(&m);
        assert!(t.contains("display closed, "), "{}: {:?}", name, t);
        assert!(!t.contains("unexpected"), "{}: {:?}", name, t);
    }
}

/// The SDK's drive code against the drive's model: the program waits
/// for the disc, reads the volume descriptor, finds a file in the root
/// directory and adds up its first megabyte.
#[test]
fn custom_program_reads_the_disc() {
    use rnse::board::Model;
    let Some(image) = disc() else { return };
    for model in [Model::Rnse192, Model::Rnse193] {
        let Some((mut m, name, custom)) = sdk_example(&image, model, "cd") else { continue };
        let ms = m.cycles_per_ms();
        // The disc "changed": the drive is becoming ready for some
        // seconds and then answers one command with a unit attention,
        // all of which the program has to get past; and it takes its
        // time over seeks and reads.
        m.atapi().times = true;
        m.atapi().insert(Some(custom.clone()));
        // (The 192's drive has to be told to start, as a unit's was
        // seen to; the SDK's wait does that.)
        assert_eq!(m.run(20_000 * ms), Stop::Budget, "{}: {:?}", name, m.cpu.fault);
        let text = String::from_utf8_lossy(&m.console).into_owned();
        // What the program should have found, from the image itself.
        let iso = fw::Iso::new(&custom).unwrap();
        let entry = iso.find("LOADING.KWI").expect("LOADING.KWI on the disc");
        let start = entry.lba as usize * 2048;
        let sum = custom[start..start + (entry.len as usize).min(0x100000)].iter().fold(0u32, |a, b| a.wrapping_add(*b as u32));
        assert!(text.contains(&format!("file at sector {}, {} bytes\r\n", entry.lba, entry.len)), "{}: {:?}", name, text);
        assert!(text.contains(&format!("sum {:08x}\r\n", sum)), "{}: sum {:08x} in {:?}", name, sum, text);
        assert!(!text.contains("unexpected"), "{}: {:?}", name, text);
    }
}

/// The drive log (examples/drive): the program has the drive push
/// the disc out when the eject key is pressed, the disc goes back in,
/// and the log has what the drive's model says on the way, with the
/// times; then SETUP hands over to the stock loader.  On a real unit
/// the same log tells what the model has to assume.
#[test]
fn drive_program_logs_the_drive() {
    use rnse::board::Model;
    let Some(image) = disc() else { return };
    for model in [Model::Rnse192, Model::Rnse193] {
        let Some((mut m, name, custom)) = sdk_example(&image, model, "drive") else { continue };
        let ms = m.cycles_per_ms();
        m.atapi().load(custom);
        assert_eq!(m.run(3_000 * ms), Stop::Budget, "{}: {:?}", name, m.cpu.fault);
        let press = |m: &mut Machine, key: &str| {
            assert!(m.panel_key(key, true));
            m.run(300 * ms);
            assert!(m.panel_key(key, false));
        };
        press(&mut m, "CD_EJECT");
        // (The 192's drive takes 3.6 s over it and says "tray closed"
        // for three more.)
        assert_eq!(m.run(8_000 * ms), Stop::Budget, "{}: {:?}", name, m.cpu.fault);
        let ejects = m.atapi().ejects;
        assert_eq!(ejects, 1, "{}: {:?}", name, String::from_utf8_lossy(&m.console));
        // Back in: not ready while the drive loads the disc, the notice
        // that the medium may have changed, and ready; on the 192, as
        // a unit's drive was seen to do it, not ready in another way
        // for a while in between.
        assert!(m.atapi().reinsert());
        assert_eq!(m.run(8_000 * ms), Stop::Budget, "{}: {:?}", name, m.cpu.fault);
        if model == Model::Rnse192 {
            // The 192's drive waits to be told to start: PREV.
            press(&mut m, "PREV");
            assert_eq!(m.run(8_000 * ms), Stop::Budget, "{}: {:?}", name, m.cpu.fault);
        }
        let text = String::from_utf8_lossy(&m.console).into_owned();
        let lines: Vec<&str> = text.lines().map(|l| l.trim()).filter(|l| l.len() > 8 && l.as_bytes()[7] == b' ' && l[..7].bytes().all(|b| b.is_ascii_digit())).collect();
        let states: Vec<&str> = lines.iter().map(|l| &l[8..]).collect();
        let time = |i: usize| lines[i][..7].parse::<u64>().unwrap();
        let loading = if model == Model::Rnse192 {
            assert_eq!(states, ["READY", "2/04/00", "6/28/00", "2/3A/01", "2/3A/00", "2/04/00", "6/28/00", "2/04/04", "START", "READY"], "{}: {:?}", name, text);
            assert!((3_500..=3_800).contains(&(time(2) - time(1))), "{}: going out from {} to {}", name, time(1), time(2));
            // Not ready for as long as it is only asked, and 5.65 s
            // more once it has been told to start, as on a unit.
            assert!(time(8) - time(7) > 5_000, "{}: started by itself after {} ms", name, time(8) - time(7));
            assert!((5_500..=5_900).contains(&(time(9) - time(8))), "{}: ready {} ms after the start", name, time(9) - time(8));
            assert!((2_600..=2_900).contains(&(time(6) - time(5))), "{}: not ready from {} to {}", name, time(5), time(6));
            5
        } else {
            assert_eq!(states, ["READY", "2/3A/00", "2/04/00", "6/28/00", "READY"], "{}: {:?}", name, text);
            assert!((5_900..=6_200).contains(&(time(3) - time(2))), "{}: not ready from {} to {}", name, time(2), time(3));
            2
        };
        assert!(!text.contains("unexpected"), "{}: {:?}", name, text);
        // The lines went out on the bus as well.
        let frames: Vec<_> = m.can().tx.iter().filter(|f| f.id == 0x7f5).cloned().collect();
        assert_eq!(frames.len(), states.len(), "{}", name);
        assert_eq!(&frames[loading].data[4..8], &[2, 4, 0, 0], "{}", name);
        // NEXT: the drive's reading speed, 256 sectors timed.
        press(&mut m, "NEXT");
        m.run(3_000 * ms);
        let text = String::from_utf8_lossy(&m.console).into_owned();
        assert!(text.contains(" READ 256 IN 000"), "{}: {:?}", name, text);
        press(&mut m, "SETUP");
        m.run(1_000 * ms);
        let text = String::from_utf8_lossy(&m.console).into_owned();
        assert!(text.contains("starting the stock loader"), "{}: {:?}", name, text);
    }
}

/// The SDK's CAN code against the controller's model: frames from the
/// bus reach the program, its own frame goes out with the stock bit
/// timing, and on a bus with nobody else it gives up as it should.
#[test]
fn custom_program_talks_on_the_can_bus() {
    use rnse::board::Model;
    use rnse::dev::hcan::Frame;
    let Some(image) = disc() else { return };
    for model in [Model::Rnse192, Model::Rnse193] {
        let Some((mut m, name, _)) = sdk_example(&image, model, "can") else { continue };
        let ms = m.cycles_per_ms();
        assert_eq!(m.run(300 * ms), Stop::Budget, "{}: {:?}", name, m.cpu.fault);
        // Two frames at once: on the wire one follows the other, and
        // the program has to take the first out of the controller
        // before the second is there.
        m.can_frame(Frame::new(0x351, &[0x45, 0, 0, 0, 0, 0, 0, 0]));
        m.can_frame(Frame::new(0x635, &[0x5a, 0]));
        assert_eq!(m.run(1_000 * ms), Stop::Budget, "{}: {:?}", name, m.cpu.fault);
        let text = String::from_utf8_lossy(&m.console).into_owned();
        for line in ["rx 351: 45 00 00 00 00 00 00 00\r\n", "rx 635: 5A 00\r\n", "tx 661: 83 01 12 A0 00 00 00 00\r\n"] {
            assert!(text.contains(line), "{}: {:?} in {:?}", name, line, text);
        }
        let stock = m.can().stock_timing();
        assert_eq!(m.can().tx_timing, Some(stock), "{}: bit timing", name);
        assert_eq!(m.can().tx.back(), Some(&Frame::new(0x661, &[0x83, 0x01, 0x12, 0xa0, 0, 0, 0, 0])), "{}", name);

        // The same on a bench, with no other node to acknowledge.
        let (mut m, name, _) = sdk_example(&image, model, "can").unwrap();
        m.can().alone = true;
        assert_eq!(m.run(1_500 * ms), Stop::Budget, "{}: {:?}", name, m.cpu.fault);
        let text = String::from_utf8_lossy(&m.console).into_owned();
        assert!(text.contains("tx not acknowledged\r\n") && !text.contains("tx 661"), "{}: {:?}", name, text);
        assert_eq!(m.can().tx_count, 0, "{}", name);
    }
}

/// The SDK's report (examples/report): the program reads the
/// processor's registers before it sets anything up and sends them,
/// the state it was started in and the boot ROM out on the CAN bus;
/// `rnse report` puts the log of frames back together.  Here the boot
/// ROM is the emulator's stand-in, and what comes back is that, byte
/// for byte.
#[test]
fn report_program_sends_the_boot_rom() {
    use rnse::board::Model;
    let Some(image) = disc() else { return };
    for model in [Model::Rnse192, Model::Rnse193] {
        let Some((mut m, name, _)) = sdk_example(&image, model, "report") else { continue };
        let ms = m.cycles_per_ms();
        // A frame takes its time on the bus: one round of the 192's
        // 48 KiB is some 15 seconds.
        let mut frames = Vec::new();
        let mut report = rnse::report::Report::default();
        for _ in 0..600 {
            assert_eq!(m.run(100 * ms), Stop::Budget, "{}: {:?}", name, m.cpu.fault);
            frames.extend(m.can().tx.drain(..));
            if frames.len() % 500 < 70 {
                report = rnse::report::Report::from_frames(&frames);
                if report.complete() {
                    break;
                }
            }
        }
        let text = String::from_utf8_lossy(&m.console).into_owned();
        assert!(text.contains("rnse sdk: report"), "{}: console: {:?}", name, text);
        assert!(report.complete(), "{}: after {} frames: {}", name, frames.len(), report.describe());
        let rom = report.rom_bytes().unwrap();
        assert_eq!(report.unit, if model == Model::Rnse192 { 0x92 } else { 0x93 }, "{}", name);
        if model == Model::Rnse192 {
            assert_eq!(rom.len(), 0xc000, "{}", name);
            // The clock register, as the emulator has it, and what the
            // report makes of it.
            assert_eq!(report.value(0xffc0_0000), Some(0x0e0a), "{}", name);
            assert!(report.describe().contains("peripherals 1/4"), "{}: {}", name, report.describe());
            // The two card processors have run the little program the
            // report loads into them: their pins as the emulator
            // straps them, and the two loops timed, which come out as
            // the emulator's clocks.
            for base in [0x10, 0x20] {
                assert_eq!(report.value(base), Some(1), "{}: {}", name, report.describe());
                assert_eq!(report.value(base + 1), Some(0x3f), "{}: {}", name, report.describe());
                // (Clock control: 0x0100 after a reset, as on a unit.)
                assert_eq!(report.value(base + 2), Some(0x100), "{}", name);
                let (t1, t2) = (report.value(base + 4).unwrap() as f64, report.value(base + 5).unwrap() as f64);
                // 1024 x 32768 clocks of 22.5792 MHz, in counts of 14.7 MHz.
                let want = 1024.0 * 32768.0 / 22_579_200.0 * 14_700_000.0;
                assert!((t1 / want - 1.0).abs() < 0.001, "{}: {} counts, {} expected", name, t1, want);
                assert!((t1 / t2 - 3.0).abs() < 0.01, "{}: {} and {} counts", name, t1, t2);
            }
            assert!(report.describe().contains("the 58.8 MHz the emulator has"), "{}: {}", name, report.describe());
            assert!(text.contains("0000003f"), "{}: console: {:?}", name, text);
        } else {
            assert!(report.value(0x10).is_none(), "{}", name);
        }
        assert_eq!(rom[..], m.board.flash[..rom.len()], "{}: the ROM that arrived", name);
        // Not much slower than the bus allows: at least 500 frames a
        // second, and never faster than a frame's 1.2 ms.
        let seconds = m.cpu.cycles as f64 / ms as f64 / 1000.0;
        let rate = frames.len() as f64 / seconds;
        assert!((400.0..850.0).contains(&rate), "{}: {} frames in {:.1} s", name, frames.len(), seconds);
        // As text, the way a log would have them.
        let log: String = frames.iter().map(|f| format!("[can] tx {}\n", f)).collect();
        let again = rnse::report::Report::from_frames(&rnse::report::frames(&log));
        assert_eq!(again.rom_bytes().unwrap(), rom, "{}", name);
        // The start state: started directly here, with interrupts masked.
        assert!(report.value(1).is_some() && report.value(5) == Some(0), "{}: {}", name, report.describe());
    }
}

/// An SDK example on a disc with a higher software index than the
/// installed one, and the installed (stock) software run until its
/// application has checked the disc, copied the program to RAM and is
/// about to call its first entry point.  That is how a real unit would
/// come to run it.  None when there is no cross compiler or the disc
/// has no software for the unit.
fn started_by_stock_software(image: &[u8], model: rnse::board::Model, example: &str) -> Option<(Machine, String, Vec<u8>)> {
    started_by_stock_software_with(image, model, example, None)
}

/// The same, with the disc in the drive from the start (None) or put
/// in after so many milliseconds, as someone would do it.
fn started_by_stock_software_with(image: &[u8], model: rnse::board::Model, example: &str, insert_at: Option<u64>) -> Option<(Machine, String, Vec<u8>)> {
    use rnse::board::Model;
    let sdk = std::path::Path::new(env!("CARGO_MANIFEST_DIR")).to_path_buf();
    let variants = fw::load_variants(image).unwrap();
    let v = variants.iter().find(|v| Model::for_variant(&v.name) == Some(model))?;
    sdk_example(image, model, example)?;
    let unit = if model == Model::Rnse192 { "192" } else { "193" };
    let program = std::fs::read(sdk.join(format!("out/{}-{}.bin", example, unit))).unwrap();
    let (mut custom, _) = fw::custom_loader(image, &v.name, &program).unwrap();
    // The disc's index, its two middle digits one up: those are what
    // the application compares.
    let index = fw::IndexInfo::from_disc(image, &v.name).unwrap();
    let middle: u32 = index.sofidx[1..3].parse().unwrap();
    let higher = format!("{}{:02}{}", &index.sofidx[..1], middle + 1, &index.sofidx[3..]);
    assert_eq!(fw::set_disc_index(&mut custom, &v.name, &higher).unwrap(), index.sofidx);

    // The stock software's own start of a loader can fail: on the
    // European 192 the copy of the loader goes over a variable of the
    // module APFM, whose timer handler (every 100 ms) then meets an
    // address error if it runs in the 15 ms before the loader has
    // taken the unit over (docs/boot-rom.md).  Whether it does is a
    // matter of timing, which on a real unit the drive makes vary and
    // which here is the same in every run.  So the start is tried at
    // the nominal ratio of CPU speed to timers and, when exactly that
    // happens, at the two next to it.
    let apfm = v.module("APFM").map(|a| a.load_addr | 0x8000_0000..(a.load_addr | 0x8000_0000) + a.size);
    let nominal = if model == Model::Rnse192 { 4 } else { 8 };
    let mut started = None;
    for ratio in [nominal, nominal + 1, nominal - 1] {
        let mut m = Machine::for_model(model);
        m.set_cpu_ratio(ratio);
        m.echo_console = false;
        // The CPU's caches are modelled all the way: the stock software has
        // to live with them as on a real unit, and so has the program.
        m.scramble_ram();
        m.cache_model();
        m.install_boot_stub(&v.name);
        m.install(v, &index);
        if insert_at.is_none() {
            m.atapi().load(custom.clone());
        }
        m.start_os(v).unwrap();
        m.set_ignition(true);
        let ms = m.cycles_per_ms();
        let entry = fw::load_variants(&custom).unwrap().iter().find(|x| x.name == v.name).unwrap().module("LDM").unwrap().entries[0];
        m.cpu.breakpoints.push(entry);
        let mut stop = Stop::Budget;
        if let Some(at) = insert_at {
            stop = m.run(at * ms);
            assert_eq!(stop, Stop::Budget, "{}: {:?}", v.name, m.cpu.fault);
            m.atapi().insert(Some(custom.clone()));
        }
        for _ in 0..40 {
            if stop != Stop::Budget {
                break;
            }
            stop = m.run(1_000 * ms);
        }
        let collision = stop == Stop::Fault && m.cpu.fault.as_deref().is_some_and(|f| {
            let spc = f.split("SPC=").nth(1).and_then(|t| u32::from_str_radix(&t[..8.min(t.len())], 16).ok());
            f.contains("EXPEVT=0e0") && spc.zip(apfm.clone()).is_some_and(|(spc, range)| range.contains(&spc))
        });
        if collision {
            eprintln!("{}: at CPU ratio {} the stock software's start of the loader collides with its own timer handler", v.name, ratio);
            continue;
        }
        assert_eq!(stop, Stop::Breakpoint, "{}: the installed software did not start the program: {:?}", v.name, m.cpu.fault);
        started = Some(m);
        break;
    }
    let mut m = started.unwrap_or_else(|| panic!("{}: the stock software's start of the loader failed at every speed tried", v.name));
    m.cpu.breakpoints.clear();
    Some((m, v.name.clone(), custom))
}

/// A disc put into the drive while the unit runs, which is how an
/// update is normally begun: the stock software of each unit notices
/// it, takes it for an update and starts its loader (here the SDK's
/// first example in the loader's place).  This depends on what the
/// drive says while it loads the disc (docs/boot-rom.md).
#[test]
fn a_disc_put_in_while_the_unit_runs_starts_the_update() {
    use rnse::board::Model;
    let Some(image) = disc() else { return };
    for (model, at) in [(Model::Rnse192, 10_000), (Model::Rnse193, 20_000)] {
        let Some((mut m, name, _)) = started_by_stock_software_with(&image, model, "hello", Some(at)) else { continue };
        let ms = m.cycles_per_ms();
        assert_eq!(m.run(2_000 * ms), Stop::Budget, "{}: {:?}", name, m.cpu.fault);
        let text = String::from_utf8_lossy(&m.console).into_owned();
        assert!(text.contains("rnse sdk: hello"), "{}: console: {:?}", name, text);
    }
}

/// The stock software of each unit takes a disc from `rnse custom` for
/// an update and starts the SDK's first example, which then has to cope
/// with the machine as a running operating system leaves it.  Nothing
/// may be written to flash on the way.
#[test]
fn stock_software_starts_a_custom_program() {
    use rnse::board::Model;
    let Some(image) = disc() else { return };
    let sdk = std::path::Path::new(env!("CARGO_MANIFEST_DIR")).to_path_buf();
    let variants = fw::load_variants(&image).unwrap();
    for model in [Model::Rnse192, Model::Rnse193] {
        let Some((mut m, name, _)) = started_by_stock_software(&image, model, "hello") else { continue };
        let ms = m.cycles_per_ms();
        // What a loader finds: privileged mode with interrupts open and
        // the operating system's handlers in place, a return address in
        // the application, the language in r4.
        assert_eq!(m.cpu.sr & (rnse::sh4::SR_MD | rnse::sh4::SR_BL | 0xf0), rnse::sh4::SR_MD, "{}: SR {:08x}", name, m.cpu.sr);
        assert_ne!(m.cpu.pr, 0, "{}", name);
        assert!(m.cpu.r[4] < 9, "{}: language {}", name, m.cpu.r[4]);
        let flash = m.board.flash.clone();
        assert_eq!(m.run(3_500 * ms), Stop::Budget, "{}: {:?}", name, m.cpu.fault);
        let text = String::from_utf8_lossy(&m.console).into_owned();
        assert!(text.contains("rnse sdk: hello") && text.contains("tick 2\r\n") && !text.contains("unexpected"), "{}: {:?}", name, &text[text.len().saturating_sub(300)..]);
        assert!(m.board.flash == flash, "{}: the program wrote to flash", name);
        // The program's picture, not the installed software's.
        let (w, h, rgb) = m.render();
        let at = |x: usize, y: usize| [rgb[(y * w + x) * 3], rgb[(y * w + x) * 3 + 1], rgb[(y * w + x) * 3 + 2]];
        let near = |p: [u8; 3], q: [u8; 3]| p.iter().zip(q).all(|(a, b)| a.abs_diff(b) <= 4);
        assert!(near(at(w * 3 / 16, h / 6), [255, 255, 0]), "{}: second colour bar is {:?}", name, at(w * 3 / 16, h / 6));
        assert!(near(at(w / 2, h * 7 / 8), [0, 0, 0]), "{}: background is {:?}", name, at(w / 2, h * 7 / 8));

        // The same disc with the index it had is no update: the
        // installed software goes on as usual.
        let v = variants.iter().find(|v| v.name == name).unwrap();
        let index = fw::IndexInfo::from_disc(&image, &name).unwrap();
        let unit = if model == Model::Rnse192 { "192" } else { "193" };
        let program = std::fs::read(sdk.join(format!("out/hello-{}.bin", unit))).unwrap();
        let (same, _) = fw::custom_loader(&image, &name, &program).unwrap();
        let mut m = Machine::for_model(model);
        m.echo_console = false;
        m.install_boot_stub(&name);
        m.install(v, &index);
        m.atapi().load(same);
        m.start_os(v).unwrap();
        m.set_ignition(true);
        assert_eq!(m.run(15_000 * ms), Stop::Budget, "{}: {:?}", name, m.cpu.fault);
        assert!(!String::from_utf8_lossy(&m.console).contains("rnse sdk"), "{}: started without a higher index", name);
    }
}

/// The other examples started the same way: the faceplate, the drive
/// and the CAN controller are then in use by the installed software up
/// to the moment the program takes over.
#[test]
fn stock_software_starts_programs_that_use_its_devices() {
    use rnse::board::Model;
    use rnse::dev::hcan::Frame;
    let Some(image) = disc() else { return };
    for model in [Model::Rnse192, Model::Rnse193] {
        // Keys and knobs.
        let Some((mut m, name, _)) = started_by_stock_software(&image, model, "keys") else { continue };
        let ms = m.cycles_per_ms();
        let resets = m.panel.resets;
        assert_eq!(m.run(600 * ms), Stop::Budget, "{}: {:?}", name, m.cpu.fault);
        assert_eq!(m.panel.resets, resets + 1, "{}: the program restarts the faceplate controller", name);
        assert!(m.panel_key("SETUP", true));
        m.run(100 * ms);
        assert!(m.panel_key("SETUP", false));
        assert!(m.panel_knob("main", -1));
        assert_eq!(m.run(300 * ms), Stop::Budget, "{}: {:?}", name, m.cpu.fault);
        let text = String::from_utf8_lossy(&m.console).into_owned();
        for line in ["faceplate online\r\n", "SETUP down\r\n", "SETUP up\r\n", "select 1 left\r\n"] {
            assert!(text.contains(line), "{}: {:?} in {:?}", name, line, &text[text.len().saturating_sub(300)..]);
        }

        // The disc.
        let (mut m, name, custom) = started_by_stock_software(&image, model, "cd").unwrap();
        assert_eq!(m.run(4_000 * ms), Stop::Budget, "{}: {:?}", name, m.cpu.fault);
        let text = String::from_utf8_lossy(&m.console).into_owned();
        let entry = fw::Iso::new(&custom).unwrap().find("LOADING.KWI").unwrap();
        let start = entry.lba as usize * 2048;
        let sum = custom[start..start + (entry.len as usize).min(0x100000)].iter().fold(0u32, |a, b| a.wrapping_add(*b as u32));
        assert!(text.contains(&format!("sum {:08x}\r\n", sum)), "{}: sum {:08x} in {:?}", name, sum, &text[text.len().saturating_sub(300)..]);

        // CAN.
        let (mut m, name, _) = started_by_stock_software(&image, model, "can").unwrap();
        m.set_ignition(false); // no more frames but the one below
        assert_eq!(m.run(1_300 * ms), Stop::Budget, "{}: {:?}", name, m.cpu.fault);
        m.can_frame(Frame::new(0x635, &[0x5a, 0]));
        assert_eq!(m.run(300 * ms), Stop::Budget, "{}: {:?}", name, m.cpu.fault);
        let text = String::from_utf8_lossy(&m.console).into_owned();
        for line in ["rx 635: 5A 00\r\n", "tx 661: 83 01 12 A0 00 00 00 00\r\n"] {
            assert!(text.contains(line), "{}: {:?} in {:?}", name, line, &text[text.len().saturating_sub(300)..]);
        }
        let stock = m.can().stock_timing();
        assert_eq!(m.can().tx_timing, Some(stock), "{}: bit timing", name);
        assert!(!text.contains("unexpected"), "{}: {:?}", name, text);
    }
}

/// The way back.  The stock software starts the SDK's probe from a disc
/// with a higher index; the probe counts down and hands over to the
/// stock loader that `rnse custom` packed into its image; the stock
/// loader updates the unit from the disc and resets it.  Afterwards the
/// flash holds the disc's software with the disc's index, so that the
/// disc is no update any more.
#[test]
fn custom_program_hands_back_to_the_stock_loader() {
    use rnse::board::Model;
    let Some(image) = disc() else { return };
    let variants = fw::load_variants(&image).unwrap();
    for model in [Model::Rnse192, Model::Rnse193] {
        let Some((mut m, name, custom)) = started_by_stock_software(&image, model, "probe") else { continue };
        let ms = m.cycles_per_ms();
        // A minute of counting down, then the update.
        let stop = m.run(240_000 * ms);
        let text = String::from_utf8_lossy(&m.console).into_owned();
        let tail = &text[text.len().saturating_sub(300)..];
        assert!(text.contains("the stock loader is carried along\r\n") && text.contains("starting the stock loader\r\n"), "{}: {:?}", name, tail);
        assert!(!text.contains("did not unpack") && !text.contains("unexpected"), "{}: {:?}", name, tail);
        assert_eq!(stop, Stop::Fault, "{}: the stock loader ends with a reset: {:?}", name, tail);
        assert!(m.reset_requested, "{}: {:?}", name, m.cpu.fault);
        let cache = &m.cpu.cache.trouble;
        assert_eq!((cache.stale_fetches, cache.dirty_discarded), (0, 0), "{}: {:?}", name, cache);
        // What the stock loader installed: the disc's modules, and a
        // directory with the disc's (raised) index.
        let v = variants.iter().find(|v| v.name == name).unwrap();
        for module in v.modules.iter().filter(|x| x.kind != "LDR") {
            let (a, n) = (module.phys_addr() as usize, module.size as usize);
            assert!(m.board.flash[a..a + n] == module.data[..n], "{}: module {} as written by the stock loader", name, module.name);
        }
        let index = fw::IndexInfo::from_disc(&custom, &name).unwrap();
        assert_ne!(index.sofidx, fw::IndexInfo::from_disc(&image, &name).unwrap().sofidx);
        let dir = fw::module_directory_for(v, &index);
        let at = v.module("OSM").unwrap().phys_addr() as usize - dir.len();
        assert!(m.board.flash[at..at + dir.len()] == dir[..], "{}: module directory with index {}", name, index.sofidx);
    }
}

/// A boot ROM read from a 192 (`rnse report --rom`; firmware, so not in
/// the repository): `RNSE_BOOT_ROM` names the file.
fn boot_rom() -> Option<Vec<u8>> {
    let path = std::env::var_os("RNSE_BOOT_ROM")?;
    Some(std::fs::read(path).expect("RNSE_BOOT_ROM is not readable"))
}

/// A 192 as `started_192` makes it, under a unit's boot ROM, with the
/// software of `flash` installed and `drive` in the drive.
fn rom_192(flash: &[u8], drive: &[u8], rom: &[u8]) -> Option<Machine> {
    use rnse::board::Model;
    let variants = fw::load_variants(flash).unwrap();
    let v = variants.iter().find(|v| Model::for_variant(&v.name) == Some(Model::Rnse192))?;
    let index = fw::IndexInfo::from_disc(flash, &v.name).unwrap();
    let mut m = Machine::new();
    m.echo_console = false;
    m.scramble_ram();
    m.cache_model();
    m.install_boot_stub(&v.name);
    m.install(v, &index);
    m.atapi().load(drive.to_vec());
    m.set_ignition(true);
    if let Some(address) = Machine::known_printf("V2220371") {
        m.hook_printf(address);
    }
    m.start_from_reset(rom).unwrap();
    Some(m)
}

/// Run until the log has `what` after `from`, in steps of five seconds.
fn until(m: &mut Machine, from: usize, what: &str, seconds: u64) -> Option<usize> {
    let ms = m.cycles_per_ms();
    for _ in 0..seconds / 5 {
        assert_eq!(m.run(5_000 * ms), Stop::Budget, "{:?}", m.cpu.fault);
        let text = String::from_utf8_lossy(&m.console);
        if let Some(at) = text.get(from..).and_then(|t| t.find(what)) {
            return Some(from + at);
        }
    }
    None
}

/// As `until`, in steps of 100 ms: for a test that must act soon after
/// the text appears (a program's own timer already running), not up to
/// five seconds later.
fn until_soon(m: &mut Machine, from: usize, what: &str, seconds: u64) -> Option<usize> {
    let ms = m.cycles_per_ms();
    for _ in 0..seconds * 10 {
        assert_eq!(m.run(100 * ms), Stop::Budget, "{:?}", m.cpu.fault);
        if let Some(at) = m.console.get(from..).and_then(|t| String::from_utf8_lossy(t).find(what)) {
            return Some(from + at);
        }
    }
    None
}

fn stock_is_in_flash(m: &Machine, image: &[u8]) {
    use rnse::board::Model;
    let variants = fw::load_variants(image).unwrap();
    let v = variants.iter().find(|v| Model::for_variant(&v.name) == Some(Model::Rnse192)).unwrap();
    for module in v.modules.iter().filter(|x| x.kind != "LDR") {
        let at = module.phys_addr() as usize;
        assert!(m.board.flash[at..at + module.size as usize] == module.data[..module.size as usize], "{} is not the stock module", module.name);
    }
}

/// The disc `rnse firmware` makes of an SDK program, with a higher
/// index than the stock disc's.
fn firmware_disc(image: &[u8], example: &str) -> Option<Vec<u8>> {
    use rnse::board::Model;
    let sdk = std::path::Path::new(env!("CARGO_MANIFEST_DIR")).to_path_buf();
    let variants = fw::load_variants(image).unwrap();
    let v = variants.iter().find(|v| Model::for_variant(&v.name) == Some(Model::Rnse192))?;
    sdk_example(image, Model::Rnse192, example)?;
    let program = std::fs::read(sdk.join(format!("out/{}-192.bin", example))).unwrap();
    sdk_example(image, Model::Rnse192, "swap")?;
    let swap = std::fs::read(sdk.join("out/swap-192.bin")).unwrap();
    let mut disc = fw::firmware_disc(image, &v.name, &program, Some(&swap)).unwrap();
    let index = fw::IndexInfo::from_disc(image, &v.name).unwrap();
    let middle: u32 = index.sofidx[1..3].parse().unwrap();
    let higher = format!("{}{:02}{}", &index.sofidx[..1], middle + 1, &index.sofidx[3..]);
    fw::set_disc_index(&mut disc, &v.name, &higher).unwrap();
    Some(disc)
}

/// A program in flash (`rnse firmware`, examples/installed), all the
/// way round under a unit's boot ROM: the stock software takes the disc
/// for an update, its loader writes the program into the operating
/// system's place, the ROM starts it; and when RETURN is held the
/// program takes the directory away, the ROM finds no software and
/// installs from the disc that is in the drive by then, the stock one.
#[test]
fn a_program_is_installed_in_flash_and_taken_out_again() {
    let (Some(image), Some(rom)) = (disc(), boot_rom()) else { return };
    let Some(custom) = firmware_disc(&image, "installed") else { return };
    let Some(mut m) = rom_192(&image, &custom, &rom) else { return };
    let ms = m.cycles_per_ms();
    let tail = |m: &Machine| { let t = String::from_utf8_lossy(&m.console).into_owned(); t[t.len().saturating_sub(300)..].to_string() };
    let jump = until(&mut m, 0, "*** Jump User RESET ***", 600).unwrap_or_else(|| panic!("the stock software did not install the disc: {:?}", tail(&m)));
    let started = until(&mut m, jump, "rnse sdk: installed", 60).unwrap_or_else(|| panic!("the ROM did not start the program: {:?}", tail(&m)));
    // It runs, shows a picture, and counts itself healthy.
    assert_eq!(m.run(12_000 * ms), Stop::Budget, "{:?}", m.cpu.fault);
    let (_, _, pixels) = m.render();
    assert!(pixels.iter().any(|&p| p != 0), "the screen is dark");
    assert_eq!(m.resets, 0, "the watchdog reset the unit");
    // The way out, with the stock disc in the drive.
    m.atapi().load(image.clone());
    assert!(m.panel_key("RETURN", true));
    assert_eq!(m.run(3_000 * ms), Stop::Budget, "{:?}", m.cpu.fault);
    assert!(m.panel_key("RETURN", false));
    let removed = until(&mut m, started, "removed, restarting", 10).unwrap_or_else(|| panic!("not removed: {:?}", tail(&m)));
    let back = until(&mut m, removed, "*** Jump User RESET ***", 600).unwrap_or_else(|| panic!("the ROM did not install the stock disc: {:?}", tail(&m)));
    until(&mut m, back, "[fw] SOUND_TSK START", 60).unwrap_or_else(|| panic!("the stock software did not start: {:?}", tail(&m)));
    stock_is_in_flash(&m, &image);
}

/// The disc that installed a program is still in the drive when the
/// program is taken out.  The boot ROM runs the loader that the disc
/// has as a file, which on a firmware disc is not the installation but
/// examples/swap: the disc is pushed out, and when the stock disc
/// has gone in the unit restarts and the ROM installs that.
#[test]
fn a_firmware_disc_left_in_the_drive_does_not_install_again() {
    let (Some(image), Some(rom)) = (disc(), boot_rom()) else { return };
    let Some(custom) = firmware_disc(&image, "installed") else { return };
    let Some(mut m) = rom_192(&custom, &custom, &rom) else { return };
    let ms = m.cycles_per_ms();
    let tail = |m: &Machine| { let t = String::from_utf8_lossy(&m.console).into_owned(); t[t.len().saturating_sub(300)..].to_string() };
    assert_eq!(m.run(12_000 * ms), Stop::Budget, "{:?}", m.cpu.fault);
    assert!(m.panel_key("RETURN", true));
    assert_eq!(m.run(3_000 * ms), Stop::Budget, "{:?}", m.cpu.fault);
    assert!(m.panel_key("RETURN", false));
    let swap = until(&mut m, 0, "rnse sdk: swap", 60).unwrap_or_else(|| panic!("the ROM did not run the disc's other loader: {:?}", tail(&m)));
    until(&mut m, swap, "ejected", 20).unwrap_or_else(|| panic!("the disc was not pushed out: {:?}", tail(&m)));
    // It waits, for as long as it takes; the program is not installed
    // again and nothing is erased.
    let erased = m.board.flash_ctl.erased_sectors;
    assert_eq!(m.run(20_000 * ms), Stop::Budget, "{:?}", m.cpu.fault);
    assert_eq!(m.board.flash_ctl.erased_sectors, erased, "something was erased while it waited");
    assert_eq!(String::from_utf8_lossy(&m.console).matches("rnse sdk: installed").count(), 1);
    // The stock disc goes in.
    m.atapi().insert(Some(image.clone()));
    let again = until(&mut m, swap, "a disc is ready, restarting", 60).unwrap_or_else(|| panic!("the new disc was not noticed: {:?}", tail(&m)));
    let back = until(&mut m, again, "*** Jump User RESET ***", 600).unwrap_or_else(|| panic!("the ROM did not install the stock disc: {:?}", tail(&m)));
    until(&mut m, back, "[fw] SOUND_TSK START", 60).unwrap_or_else(|| panic!("the stock software did not start: {:?}", tail(&m)));
    stock_is_in_flash(&m, &image);
}

/// A firmware of one's own (examples/base), started by a unit's
/// boot ROM from flash with nothing of the stock software run before
/// it: it starts the radio processor and has the tuner as the source,
/// tunes, starts a card by itself and plays an MP3 file of it through
/// the amplifier, and goes back to the tuner.
#[test]
fn a_firmware_from_flash_is_a_radio_and_a_player() {
    use rnse::dev::radio::Radio;
    let (Some(image), Some(rom)) = (disc(), boot_rom()) else { return };
    let Some(custom) = firmware_disc(&image, "base") else { return };
    let Some(mut m) = rom_192(&custom, &image, &rom) else { return };
    let ms = m.cycles_per_ms();
    let song = include_bytes!("data/tones.mp3").to_vec();
    let card = rnse::fat::card("MUSIC", &[rnse::fat::Entry::file("TONES.MP3", song)], None).unwrap();
    assert!(m.insert_card(0, rnse::dev::sdhi::Card::from_bytes(card).unwrap()));
    let tail = |m: &Machine| { let t = String::from_utf8_lossy(&m.console).into_owned(); t[t.len().saturating_sub(300)..].to_string() };
    until(&mut m, 0, "radio started", 30).unwrap_or_else(|| panic!("the radio processor was not started: {:?}", tail(&m)));
    assert_eq!(m.run(2_000 * ms), Stop::Budget, "{:?}", m.cpu.fault);
    assert_eq!((m.radio.source, m.radio.volume), (Radio::SOURCE_TUNER, 8), "the tuner as the source");
    let press = |m: &mut Machine, key: &str| {
        assert!(m.panel_key(key, true));
        m.run(200 * ms);
        assert!(m.panel_key(key, false));
        m.run(800 * ms);
    };
    press(&mut m, "NEXT");
    assert_eq!(m.radio.frequency, Some(2062), "one megahertz up from 102.1");
    // MEDIA: the card is started from nothing, and its file plays.
    press(&mut m, "MEDIA");
    until(&mut m, 0, "playing", 30).unwrap_or_else(|| panic!("no file was played: {:?}", tail(&m)));
    assert_eq!(m.run(2_000 * ms), Stop::Budget, "{:?}", m.cpu.fault);
    assert_eq!(m.board.media.dsps[0].fault, None);
    assert!(m.board.media.dsps[0].loud_frames > 0, "the card processor sends its converter only silence");
    assert_eq!(m.radio.source, Radio::SOURCE_DECODER, "the amplifier's source");
    let (_, _, pixels) = m.render();
    assert!(pixels.iter().any(|&p| p != 0), "the screen is dark");
    assert!(m.panel_knob("volume", 3));
    assert_eq!(m.run(1_500 * ms), Stop::Budget, "{:?}", m.cpu.fault);
    assert_eq!(m.radio.volume, 11);
    press(&mut m, "RADIO");
    assert_eq!(m.radio.source, Radio::SOURCE_TUNER, "back to the tuner");
    assert_eq!(m.resets, 0, "the watchdog reset the unit");
}

/// The guard of a program in flash (src/flash.c): starts that end
/// before the program has called itself healthy are counted, and the
/// fifth in a row takes the program out, after which the boot ROM
/// installs the stock disc.  That is the way out of a program that
/// hangs, and the one by hand: off and on four times.
#[test]
fn a_program_in_flash_that_never_gets_going_takes_itself_out() {
    let (Some(image), Some(rom)) = (disc(), boot_rom()) else { return };
    let Some(custom) = firmware_disc(&image, "installed") else { return };
    // The program installed already, the stock disc in the drive.
    let Some(mut m) = rom_192(&custom, &image, &rom) else { return };
    let ms = m.cycles_per_ms();
    let count = |m: &Machine| String::from_utf8_lossy(&m.console).matches("rnse sdk: installed").count();
    // Four starts that are cut short, three seconds in.
    for n in 1..=4 {
        assert_eq!(m.run(3_000 * ms), Stop::Budget, "{:?}", m.cpu.fault);
        assert_eq!(count(&m), n, "start {}", n);
        m.cpu.reset();
    }
    // The fifth does not get to the program's main.
    let back = until(&mut m, 0, "*** Jump User RESET ***", 600).expect("the ROM did not install the stock disc");
    assert_eq!(count(&m), 4, "the program was started a fifth time");
    until(&mut m, back, "[fw] SOUND_TSK START", 60).expect("the stock software did not start");
    stock_is_in_flash(&m, &image);
    // And one that does get going is not counted out: installed anew,
    // it runs through any number of starts once it has been healthy.
    let Some(mut m) = rom_192(&custom, &image, &rom) else { return };
    for n in 1..=6 {
        assert_eq!(m.run(13_000 * ms), Stop::Budget, "{:?}", m.cpu.fault);
        assert_eq!(count(&m), n, "start {}", n);
        m.cpu.reset();
    }
}

/// The beacon (examples/beacon), the probe that says on the CAN bus
/// how far it has got: its frames in order, the faceplate controller
/// taking its settings, and the hand-over at the end.
#[test]
fn beacon_reports_its_progress_on_the_bus() {
    use rnse::board::Model;
    let Some(image) = disc() else { return };
    let Some((mut m, name, _)) = sdk_example(&image, Model::Rnse192, "beacon") else { return };
    let ms = m.cycles_per_ms();
    assert_eq!(m.run(24_500 * ms), Stop::Budget, "{}: {:?}", name, m.cpu.fault);
    let text = String::from_utf8_lossy(&m.console).into_owned();
    assert!(text.contains("starting the stock loader"), "{}: {:?}", name, text);
    assert!(!text.contains("unexpected"), "{}: {:?}", name, text);
    let frames: Vec<_> = m.can().tx.iter().filter(|f| f.id == 0x7f6).cloned().collect();
    let stages: Vec<u8> = frames.iter().map(|f| f.data[0]).collect();
    // Up; eight seconds without the faceplate; its start; fourteen with.
    let mut want = vec![1u8];
    want.extend([2; 8]);
    want.push(5);
    want.extend([2; 14]);
    // And the one it leaves in the controller for the hand-over.
    want.push(3);
    assert_eq!(stages, want, "{}", name);
    // A second apart, by the program's own clock in the frames.
    let at = |i: usize| u32::from(frames[i].data[2]) << 8 | u32::from(frames[i].data[3]);
    assert_eq!(at(22) - at(12), 1000, "{}", name);
    // The faceplate controller has its settings a second after it was started.
    assert_eq!(frames[9].data[7] & 2, 0, "{}", name);
    assert_eq!(frames[12].data[7] & 2, 2, "{}", name);
    // The state it was started in, and the latch and port A as found.
    assert_eq!(m.can().tx.iter().filter(|f| f.id == 0x7f7).count(), 9, "{}", name);
}

/// The suite (examples/suite): a row of measurements in one run,
/// every answer a record on the CAN bus.  Here against the emulator's
/// own models, which shows that the program asks properly; on a unit
/// the answers are what the models are corrected by.
#[test]
fn suite_runs_its_measurements() {
    use rnse::board::Model;
    let Some(image) = disc() else { return };
    let Some((mut m, name, custom)) = sdk_example(&image, Model::Rnse192, "suite") else { return };
    let ms = m.cycles_per_ms();
    m.atapi().load(custom);
    m.atapi().times = true;
    // Up to the thirty seconds for hands.
    let mut waited = 0;
    while !String::from_utf8_lossy(&m.console).contains("KEYS AND KNOBS") {
        assert_eq!(m.run(1_000 * ms), Stop::Budget, "{}: {:?}", name, m.cpu.fault);
        waited += 1;
        assert!(waited < 60, "{}: {:?}", name, String::from_utf8_lossy(&m.console));
    }
    m.run(3_500 * ms);
    assert!(m.panel_key("NAV", true));
    m.run(300 * ms);
    assert!(m.panel_key("NAV", false));
    assert!(m.panel_knob("main", 3));
    m.run(500 * ms);
    assert!(m.panel_key("RETURN", true));
    m.run(300 * ms);
    assert!(m.panel_key("RETURN", false));
    // No disc change (RETURN leaves it out), and of the battery-backed
    // memory only the beginning (RETURN held down).
    let until = |m: &mut Machine, what: &str| {
        let mut waited = 0;
        while !String::from_utf8_lossy(&m.console).contains(what) {
            assert_eq!(m.run(200 * ms), Stop::Budget, "{:?}", m.cpu.fault);
            waited += 1;
            assert!(waited < 300, "{}: {:?}", what, String::from_utf8_lossy(&m.console));
        }
    };
    until(&mut m, "A DISC CHANGE");
    m.run(500 * ms);
    assert!(m.panel_key("SETUP", true));
    until(&mut m, "BATTERY-BACKED MEMORY");
    until(&mut m, "BYTES SENT");
    assert!(m.panel_key("SETUP", false));
    // The watchdog test ends the run: the emulator's bites as a unit's does.
    let stop = m.run(8_000 * ms);
    let text = String::from_utf8_lossy(&m.console).into_owned();
    assert_eq!(stop, Stop::Fault, "{}: {:?}", name, text);
    assert!(m.reset_requested, "{}: {:?}", name, m.cpu.fault);
    assert!(!text.contains("unexpected"), "{}: {:?}", name, text);
    let records: Vec<(u8, u8, u32, u16)> = m.can().tx.iter().filter(|f| f.id == 0x7f8).map(|f| {
        let d = f.data;
        (d[0], d[1], u32::from_be_bytes([d[2], d[3], d[4], d[5]]), u16::from_be_bytes([d[6], d[7]]))
    }).collect();
    let value = |test: u8, item: u8| records.iter().find(|r| r.0 == test && r.1 == item).map(|r| r.2);
    // Every test was opened, in order.
    let opened: Vec<u8> = records.iter().filter(|r| r.1 == 0xfe).map(|r| r.0).collect();
    assert_eq!(opened, [1, 2, 3, 4, 5, 6, 7, 8, 11, 12, 13, 14, 15, 16, 9, 19, 17, 18, 10], "{}", name);
    // The display folded down, stopped by the driver at its end and
    // open (state 1), its switch saying so; and up again (state 0).
    let second = |test: u8, item: u8| records.iter().find(|r| r.0 == test && r.1 == item).map(|r| r.3);
    assert_eq!(second(19, 2), Some(1), "{}: {:?}", name, records.iter().filter(|r| r.0 == 19).collect::<Vec<_>>());
    assert!(value(19, 3).is_some_and(|p| p <= 94) && second(19, 3) == Some(1), "{}: open at {:?}", name, value(19, 3));
    assert!(value(19, 2).is_some_and(|t| (1_000..3_000).contains(&t)), "{}: the motor ran {:?} ms", name, value(19, 2));
    assert!(records.iter().filter(|r| r.0 == 19 && r.1 == 1).count() >= 10, "{}: samples while it moved", name);
    assert_eq!(second(19, 5), Some(0), "{}", name);
    assert!(value(19, 6).is_some_and(|p| p >= 894) && second(19, 6) == Some(0), "{}: closed at {:?}", name, value(19, 6));
    assert_eq!(m.display_motor(), 0, "{}", name);
    // The radio processor was asked its two questions.  (Its line is
    // the installed software's to set up; started like this, without
    // that software, the program hears nothing on it.)  And the first
    // flash chip said what it is.
    assert!(value(12, 62).is_some() && value(12, 126).is_some(), "{}", name);
    assert_eq!((value(13, 0), value(13, 1)), (Some(0x04), Some(0x2222_227e)), "{}", name);
    // "QRY", and a sector erased in 2^8 ms.
    assert_eq!((value(13, 2), value(13, 3), value(13, 4), value(13, 19)), (Some(0x51), Some(0x52), Some(0x59), Some(8)), "{}", name);
    // The drive's name once more, in the full answer.
    assert_eq!(value(14, 2), Some(u32::from_be_bytes(*b"ALPI")), "{}", name);
    // Some of the battery-backed memory went out on its own frames.
    assert!(m.can().tx.iter().any(|f| f.id == 0x7f9), "{}", name);
    // The loop takes some 14 million instructions' time at 235.2 MHz.
    let spin = value(1, 0).unwrap();
    assert!((40_000..90_000).contains(&spin), "{}: the loop took {} us", name, spin);
    assert!(value(1, 1).unwrap() > 2_000, "{}: adding up took {:?} us", name, value(1, 1));
    // The display is closed: input 3 at its top.
    assert_eq!(value(2, 3), Some(1023), "{}", name);
    // Port 0 is the strobe's, an output; the latch is as the ROM and
    // board_init leave it.
    assert_eq!(records.iter().find(|r| r.0 == 3 && r.1 == 0).map(|r| r.3), Some(1), "{}", name);
    assert_eq!(value(3, 17), Some(0x3f9f), "{}", name);
    // The clock chip is not running, as on a unit.
    assert_eq!(value(4, 0), Some(0), "{}", name);
    assert!(value(5, 0).unwrap() < 1_000, "{}: faceplate after {:?} ms", name, value(5, 0));
    // The drive: the disc's size, and 64 sectors at 5.5 ms each.
    assert_eq!(value(7, 9), Some(image.len() as u32 / 2048), "{}", name);
    for item in 10..=13 {
        let t = value(7, item).unwrap();
        assert!((330_000..650_000).contains(&t), "{}: read {} took {} us", name, item, t);
    }
    // Its name, as a unit's drive gave it.
    assert_eq!(value(7, 2), Some(u32::from_be_bytes(*b"ALPI")), "{}", name);
    // The key and the knob were seen.
    assert!(records.iter().any(|r| r.0 == 9 && r.1 == 0 && r.2 & 1 << 2 != 0), "{}: NAV not seen", name);
    let knob: Vec<i32> = records.iter().filter(|r| r.0 == 9 && r.1 == 2).map(|r| r.2 as i32).collect();
    assert!(knob.last().is_some_and(|v| v.abs() == 3), "{}: the knob's three steps not seen: {:?}", name, knob);
    // And the watchdog: records every 10 ms until it bites, after some
    // 110 ms as on a unit.
    let last = records.iter().filter(|r| r.0 == 10 && r.1 == 0).map(|r| r.2).max().unwrap();
    assert!((90..=130).contains(&last), "{}: reset {} ms after the last strobe", name, last);
}

/// Sound from a program of the SDK (examples/player): started from
/// a card by the loader, it has the card processor play an MP3 file of
/// that card and the radio processor's amplifier take the converter as
/// its source, and what leaves the speaker is the file's two tones.
#[test]
fn sdk_player_plays_an_mp3() {
    use rnse::board::Model;
    use rnse::dev::radio::Radio;
    let Some(image) = disc() else { return };
    let sdk = std::path::Path::new(env!("CARGO_MANIFEST_DIR")).to_path_buf();
    let Some((mut m, name, _)) = started_by_stock_software(&image, Model::Rnse192, "sdboot") else { return };
    if sdk_example(&image, Model::Rnse192, "player").is_none() { return }
    let player = std::fs::read(sdk.join("out/player-192.bin")).unwrap();
    let song = include_bytes!("data/tones.mp3").to_vec();
    let entries = vec![rnse::fat::Entry::file("RNSE.BIN", player), rnse::fat::Entry::file("TONES.MP3", song)];
    let card = rnse::fat::card("MUSIC", &entries, None).unwrap();
    assert!(m.insert_card(0, rnse::dev::sdhi::Card::from_bytes(card).unwrap()));
    let ms = m.cycles_per_ms();
    m.cpu.breakpoints.clear();
    assert_eq!(m.run(12_000 * ms), Stop::Budget, "{}: {:?}", name, m.cpu.fault);
    let text = String::from_utf8_lossy(&m.console).into_owned();
    let tail = &text[text.len().saturating_sub(400)..];
    assert!(text.contains("rnse sdk: player"), "{}: {:?}", name, tail);
    assert!(text.contains("playing: TONES   MP3"), "{}: {:?}", name, tail);
    assert!(!text.contains("not played"), "{}: {:?}", name, tail);
    let dsp = &m.board.media.dsps[0];
    assert_eq!(dsp.fault, None, "{}", name);
    assert!(dsp.loud_frames > 0, "{}: the card processor sends its converter only silence: {:?}", name, tail);
    assert_eq!((m.radio.source, m.radio.volume), (Radio::SOURCE_DECODER, 8), "{}: the amplifier's source and volume", name);
    // A second of it, and the tones where they belong.
    m.speaker = Some(Vec::new());
    assert_eq!(m.run(1_000 * ms), Stop::Budget, "{}: {:?}", name, m.cpu.fault);
    let heard = m.speaker.take().unwrap();
    assert!(heard.len() > 80_000, "{}: {} samples in a second", name, heard.len());
    for (channel, want) in [(0usize, 440.0f64), (1, 880.0)] {
        let samples: Vec<f64> = heard.iter().skip(channel).step_by(2).map(|&v| f64::from(v)).collect();
        let crossings = samples.windows(2).filter(|w| w[0] < 0.0 && w[1] >= 0.0).count() as f64;
        let pitch = crossings / (samples.len() as f64 / 44_100.0);
        assert!((pitch - want).abs() < want * 0.05, "{}: channel {} sounds at {} Hz, not {}", name, channel, pitch, want);
    }
    // The volume knob is the amplifier's.
    assert!(m.panel_knob("volume", 2));
    assert_eq!(m.run(1_500 * ms), Stop::Budget, "{}: {:?}", name, m.cpu.fault);
    assert_eq!(m.radio.volume, 10, "{}: two detents louder", name);
}

/// The SD loader's menu (examples/sdboot): a card with several
/// programs (NAME.BIN) offers them, a loader among them as RNSE.BIN
/// left out; the select knob chooses, SELECT runs, the program comes
/// back to the menu with it chosen, and RETURN runs it again.  And a
/// loader chosen from the menu takes over from the one on the disc, as
/// it does on a unit whose disc has an older loader that runs RNSE.BIN.
/// The SD loader opens its card again after a program and after a card
/// swap the way the stock software would: the card as it was left
/// (sd_attach, CAN item 4); a card that has gone in since, and that the
/// loader saw go in at once, started on the processor's program as it
/// runs (sd_restart, item 6), before anything else is said to it; and
/// only then the processor reset as well (sd_init).  The stock driver
/// starts a card that goes in without a reset.  (A unit's 512 MB card
/// gave "LOAD ERROR: -7" after a swap and after Quake; the cause was a
/// CMD55 to the card once it was ready, and sd.c now starts it again as the stock
/// driver does: docs/hardware.md.)  The emulator's card answers either way, so
/// what is checked is which way the loader took.  And RETURN held while
/// the program reads its card without a pause (examples/cardloop,
/// as Quake reads) waits for the end of the read (sd_busy).
#[test]
fn sd_loader_reopens_its_card_after_a_return_or_a_swap() {
    use rnse::board::Model;
    let Some(image) = disc() else { return };
    let sdk = std::path::Path::new(env!("CARGO_MANIFEST_DIR")).to_path_buf();
    let Some((mut m, name, _)) = started_by_stock_software(&image, Model::Rnse192, "sdboot") else { return };
    let loader = std::fs::read(sdk.join("out/sdboot-192.bin")).unwrap();
    assert!(sdk_example(&image, Model::Rnse192, "cardloop").is_some());
    let reader = std::fs::read(sdk.join("out/cardloop-192.bin")).unwrap();
    assert!(sdk_example(&image, Model::Rnse192, "returntest").is_some());
    let r17 = std::fs::read(sdk.join("out/returntest-192.bin")).unwrap();
    let ms = m.cycles_per_ms();
    m.cpu.breakpoints.clear();
    assert_eq!(m.run(2000 * ms), Stop::Budget);
    let card = rnse::fat::card("READS", &[
        rnse::fat::Entry::file("RNSE.BIN", loader),
        rnse::fat::Entry::file("LOOP.BIN", reader),
        rnse::fat::Entry::file("A17.BIN", r17),
    ], None).unwrap();
    let at = m.console.len();
    assert!(m.insert_card(0, rnse::dev::sdhi::Card::from_bytes(card).unwrap()));
    let menu = until(&mut m, at, "> LOOP.BIN", 30).unwrap_or_else(|| panic!("{}: no menu: {}", name, String::from_utf8_lossy(&m.console[at..])));
    assert!(m.panel_key("SELECT", true));
    assert_eq!(m.run(200 * ms), Stop::Budget);
    assert!(m.panel_key("SELECT", false));
    let reading = until(&mut m, menu, "READS 1024", 30).unwrap_or_else(|| panic!("{}: {}", name, String::from_utf8_lossy(&m.console[menu..])));
    // A few times over, so that RETURN comes in a read at different
    // places of it.
    let mut from = reading;
    for round in 0..3 {
        assert!(m.panel_key("RETURN", true));
        assert_eq!(m.run((2_500 + 37 * round) * ms), Stop::Budget);
        assert!(m.panel_key("RETURN", false));
        let back = until(&mut m, from, "TEST ENDED BY HOLDING RETURN", 30).unwrap_or_else(|| panic!("{}: round {}: {}", name, round, String::from_utf8_lossy(&m.console[from..])));
        let shown = until(&mut m, back, "> LOOP.BIN", 30).unwrap_or_else(|| panic!("{}: round {}: no menu: {}", name, round, String::from_utf8_lossy(&m.console[back..])));
        let attached: Vec<u32> = m.can().tx.iter().filter(|f| f.id == 0x7f8 && f.data[0] == 0x20 && f.data[1] == 4)
            .map(|f| u32::from_be_bytes([f.data[2], f.data[3], f.data[4], f.data[5]])).collect();
        assert_eq!(attached.last(), Some(&0), "{}: round {}: the loader could not take the card as the program left it: {:?}", name, round, attached);
        if round == 2 {
            break;
        }
        assert_eq!(m.run(1000 * ms), Stop::Budget);
        assert!(m.panel_key("SELECT", true));
        assert_eq!(m.run(200 * ms), Stop::Budget);
        assert!(m.panel_key("SELECT", false));
        from = until(&mut m, shown, "READS 512", 30).unwrap_or_else(|| panic!("{}: round {}: {}", name, round, String::from_utf8_lossy(&m.console[shown..])));
    }
    // A card swap: the card that goes in is started without a reset of
    // its processor.
    let records = |m: &mut Machine, item: u8| -> Vec<i32> {
        m.can().tx.iter().filter(|f| f.id == 0x7f8 && f.data[0] == 0x20 && f.data[1] == item)
            .map(|f| i32::from_be_bytes([f.data[2], f.data[3], f.data[4], f.data[5]])).collect()
    };
    let restarts = records(&mut m, 6).len();
    let attaches = records(&mut m, 4).len();
    m.eject_card(0);
    assert_eq!(m.run(1000 * ms), Stop::Budget);
    let card = rnse::fat::card("SWAPPED", &[
        rnse::fat::Entry::file("LOOP.BIN", std::fs::read(sdk.join("out/cardloop-192.bin")).unwrap()),
        rnse::fat::Entry::file("A17.BIN", std::fs::read(sdk.join("out/returntest-192.bin")).unwrap()),
    ], None).unwrap();
    let at = m.console.len();
    assert!(m.insert_card(0, rnse::dev::sdhi::Card::from_bytes(card).unwrap()));
    until(&mut m, at, "> LOOP.BIN", 30).unwrap_or_else(|| panic!("{}: the new card: {}", name, String::from_utf8_lossy(&m.console[at..])));
    // (Not asked for a sector first: the loader saw it go in.)
    let attached = records(&mut m, 4);
    let restarted = records(&mut m, 6);
    assert_eq!(attached.len(), attaches, "{}: a card just put in was read before its start: {:?}", name, attached);
    assert_eq!((restarted.len(), restarted.last()), (restarts + 1, Some(&0)), "{}: the new card was not started without a reset", name);
    assert_eq!(records(&mut m, 0).last(), Some(&0), "{}", name);
    // (The first card, too, went in under the loader's eyes: restarted.)
    assert_eq!(records(&mut m, 6).first(), Some(&0), "{}", name);
    let text = String::from_utf8_lossy(&m.console[reading..]).into_owned();
    assert!(!text.contains("LOAD ERROR") && !text.contains("ATTACH FAILED"), "{}: {}", name, &text[text.len().saturating_sub(1500)..]);
}

#[test]
fn sd_loader_offers_a_menu_of_programs() {
    use rnse::board::Model;
    let Some(image) = disc() else { return };
    let sdk = std::path::Path::new(env!("CARGO_MANIFEST_DIR")).to_path_buf();
    let Some((mut m, name, _)) = started_by_stock_software(&image, Model::Rnse192, "sdboot") else { return };
    let loader = std::fs::read(sdk.join("out/sdboot-192.bin")).unwrap();
    assert!(sdk_example(&image, Model::Rnse192, "returntest").is_some());
    let r17 = std::fs::read(sdk.join("out/returntest-192.bin")).unwrap();
    assert!(sdk_example(&image, Model::Rnse192, "returnapi").is_some());
    let r23 = std::fs::read(sdk.join("out/returnapi-192.bin")).unwrap();
    let flash = m.board.flash.clone();
    let ms = m.cycles_per_ms();
    m.cpu.breakpoints.clear();
    assert_eq!(m.run(2000 * ms), Stop::Budget);
    let press = |m: &mut Machine, key: &str| {
        assert!(m.panel_key(key, true));
        assert_eq!(m.run(200 * m.cycles_per_ms()), Stop::Budget);
        assert!(m.panel_key(key, false));
        assert_eq!(m.run(300 * m.cycles_per_ms()), Stop::Budget);
    };
    let card = rnse::fat::card("TESTS", &[
        rnse::fat::Entry::file("RNSE.BIN", loader.clone()),
        rnse::fat::Entry::file("A17.BIN", r17.clone()),
        rnse::fat::Entry::file("B23.BIN", r23),
    ], None).unwrap();
    let at = m.console.len();
    assert!(m.insert_card(0, rnse::dev::sdhi::Card::from_bytes(card).unwrap()));
    let menu = until(&mut m, at, "> A17.BIN", 30).unwrap_or_else(|| panic!("{}: no menu: {}", name, String::from_utf8_lossy(&m.console[at..])));
    let shown = String::from_utf8_lossy(&m.console[at..]).into_owned();
    assert!(shown.contains("  B23.BIN") && !shown.contains("RNSE.BIN"), "{}: {}", name, shown);
    assert!(!shown.contains("rnse sdk: returntest"), "{}: nothing runs by itself", name);
    // The display key works with the menu up: open, then closed again
    // (CLAUDE.md, "Mandatory hardware behavior").
    press(&mut m, "EJECT");
    assert_eq!(m.run(3000 * ms), Stop::Budget);
    assert!(m.board.display_pos <= 94, "{}: the display did not open from the menu: {}", name, m.board.display_pos);
    press(&mut m, "EJECT");
    assert_eq!(m.run(3000 * ms), Stop::Budget);
    assert!(m.board.display_pos >= 894, "{}: the display did not close: {}", name, m.board.display_pos);
    // The knob to the second, SELECT: it runs and comes back to the
    // menu, with it chosen.
    assert!(m.panel_knob("main", 1));
    assert_eq!(m.run(300 * ms), Stop::Budget);
    press(&mut m, "SELECT");
    let back = until(&mut m, menu, "TEST RETURNED: 23", 60).unwrap_or_else(|| panic!("{}: {}", name, String::from_utf8_lossy(&m.console[menu..])));
    until(&mut m, back, "> B23.BIN", 30).unwrap_or_else(|| panic!("{}: not back at the menu: {}", name, String::from_utf8_lossy(&m.console[back..])));
    // RETURN: the same again.
    assert_eq!(m.run(1500 * ms), Stop::Budget);
    let at = m.console.len();
    press(&mut m, "RETURN");
    until(&mut m, at, "TEST RETURNED: 23", 60).unwrap_or_else(|| panic!("{}: RETURN does not rerun: {}", name, String::from_utf8_lossy(&m.console[at..])));
    // Back to the first.
    let at = m.console.len();
    assert_eq!(m.run(1500 * ms), Stop::Budget);
    assert!(m.panel_knob("main", -1));
    assert_eq!(m.run(300 * ms), Stop::Budget);
    press(&mut m, "SELECT");
    until(&mut m, at, "TEST RETURNED: 17", 60).unwrap_or_else(|| panic!("{}: {}", name, String::from_utf8_lossy(&m.console[at..])));
    let text = String::from_utf8_lossy(&m.console).into_owned();
    assert_eq!(text.matches("rnse sdk: returntest").count(), 3, "{}", name);
    m.eject_card(0);
    assert_eq!(m.run(500 * ms), Stop::Budget);
    // The card out: the menu goes, all of it (a blank of the wrong
    // height left the glyphs' bottom row behind, on a unit).
    let (w, _, rgb) = m.render();
    let w = w as usize;
    for y in 30 + 14 * 2..30 + 14 * 13 {
        assert!(rgb[3 * w * y..3 * w * (y + 1)].iter().all(|&c| c == 0), "{}: the menu left something on line {} of the screen", name, y);
    }

    // A loader under another name, chosen from the menu, becomes the
    // one programs return to.
    let card = rnse::fat::card("CHAIN", &[
        rnse::fat::Entry::file("LOADER.BIN", loader),
        rnse::fat::Entry::file("A17.BIN", r17),
    ], None).unwrap();
    let at = m.console.len();
    assert!(m.insert_card(0, rnse::dev::sdhi::Card::from_bytes(card).unwrap()));
    // (A17, run last, is chosen: this card has it too.)
    until(&mut m, at, "> A17.BIN", 30).unwrap_or_else(|| panic!("{}: {}", name, String::from_utf8_lossy(&m.console[at..])));
    assert!(m.panel_knob("main", -1));
    assert_eq!(m.run(300 * ms), Stop::Budget);
    let menu = until(&mut m, at, "> LOADER.BIN", 30).unwrap_or_else(|| panic!("{}: {}", name, String::from_utf8_lossy(&m.console[at..])));
    press(&mut m, "SELECT");
    // The loader from the card starts, finds the card in, and offers
    // its menu; from there, A17.
    let started = until(&mut m, menu, "rnse sdk: sdboot", 30).expect("the loader from the card starts");
    until(&mut m, started, "> LOADER.BIN", 30).unwrap_or_else(|| panic!("{}: {}", name, String::from_utf8_lossy(&m.console[started..])));
    assert!(m.panel_knob("main", 1));
    assert_eq!(m.run(300 * ms), Stop::Budget);
    press(&mut m, "SELECT");
    until(&mut m, started, "TEST RETURNED: 17", 60).unwrap_or_else(|| panic!("{}: {}", name, String::from_utf8_lossy(&m.console[started..])));
    assert_eq!(m.board.flash, flash, "{}", name);
    let text = String::from_utf8_lossy(&m.console).into_owned();
    assert!(!text.contains("unexpected") && !text.contains("LOAD ERROR"), "{}: {}", name, &text[text.len().saturating_sub(1500)..]);
    let cache = &m.cpu.cache.trouble;
    assert_eq!((cache.stale_fetches, cache.dirty_discarded), (0, 0), "{}: {:?}", name, cache);
}

/// Sound as samples from a program of our own (src/pcm.c,
/// examples/pcmtest): each card processor in turn gets the SDK's
/// player and plays two tones, fed by one DMA transfer and by DMA in
/// blocks, and the amplifier gives out each at its pitch, for its two
/// seconds, at the converter's 44.1 kHz, without a break.
#[test]
fn sdk_pcm_plays_through_both_card_processors() {
    use rnse::board::Model;
    use rnse::dev::radio::Radio;
    let Some(image) = disc() else { return };
    let sdk = std::path::Path::new(env!("CARGO_MANIFEST_DIR")).to_path_buf();
    let Some((mut m, name, _)) = started_by_stock_software(&image, Model::Rnse192, "sdboot") else { return };
    if sdk_example(&image, Model::Rnse192, "pcmtest").is_none() { return }
    let program = std::fs::read(sdk.join("out/pcmtest-192.bin")).unwrap();
    let card = rnse::fat::card("SOUND", &[rnse::fat::Entry::file("RNSE.BIN", program)], None).unwrap();
    assert!(m.insert_card(0, rnse::dev::sdhi::Card::from_bytes(card).unwrap()));
    let flash = m.board.flash.clone();
    let ms = m.cycles_per_ms();
    m.cpu.breakpoints.clear();
    m.speaker = Some(Vec::new());
    let text = loop {
        assert_eq!(m.run(1_000 * ms), Stop::Budget, "{}: {:?}", name, m.cpu.fault);
        let text = String::from_utf8_lossy(&m.console).into_owned();
        if text.contains("DONE.") || m.cpu.cycles > 60_000 * ms {
            break text;
        }
    };
    // (What the converters sent last reaches the speaker a little later.)
    assert_eq!(m.run(1_000 * ms), Stop::Budget, "{}: {:?}", name, m.cpu.fault);
    let tail = &text[text.len().saturating_sub(1500)..];
    assert!(text.contains("rnse sdk: pcmtest"), "{}: {:?}", name, tail);
    assert!(!text.contains("FAILED") && !text.contains("DID NOT START"), "{}: {:?}", name, tail);
    let rates: Vec<u32> = text.lines().filter_map(|l| l.trim().strip_prefix("SAMPLES A SECOND ")).filter_map(|v| v.trim().parse().ok()).collect();
    assert_eq!(rates.len(), 4, "{}: {:?}", name, tail);
    assert!(rates.iter().all(|r| r.abs_diff(44_100) <= 20), "{}: every way at 44.1 kHz: {:?}", name, rates);
    assert_eq!(m.radio.source, Radio::SOURCE_DECODER, "{}", name);
    for d in &m.board.media.dsps {
        assert_eq!(d.fault, None, "{}: {}", name, d.name);
    }
    // The pitch of each half second of the right channel: the six
    // tones, in their order, each for its two seconds.
    let heard = m.speaker.take().unwrap();
    let right: Vec<i16> = heard.iter().skip(1).step_by(2).copied().collect();
    let pitches: Vec<u32> = right.chunks(22_050).map(|c| 2 * c.windows(2).filter(|w| w[0] < 0 && w[1] >= 0).count() as u32).collect();
    for want in [440, 550, 880, 1100] {
        let halves = pitches.iter().filter(|&&p| p.abs_diff(want) <= 4).count();
        assert!(halves >= 3, "{}: {} Hz heard for {} half seconds: {:?}", name, want, halves, pitches);
    }
    // No break in a tone: within each half second that is one tone
    // throughout, no step between two samples larger than that tone's
    // sine makes.  (Where one tone ends and the next starts, a step is
    // what it is.)
    let chunks: Vec<&[i16]> = right.chunks(22_050).collect();
    for k in 1..chunks.len().saturating_sub(1) {
        let (chunk, pitch) = (chunks[k], pitches[k]);
        let Some(&hz) = [440u32, 550, 880, 1100].iter().find(|&&f| pitch.abs_diff(f) <= 4) else { continue };
        // (The same tone on both sides: not where one starts or ends.)
        if pitches[k - 1].abs_diff(hz) > 4 || pitches[k + 1].abs_diff(hz) > 4 {
            continue;
        }
        let peak = chunk.iter().map(|v| i32::from(*v).abs()).max().unwrap();
        let largest = chunk.windows(2).map(|w| (i32::from(w[1]) - i32::from(w[0])).abs()).max().unwrap();
        // (pcmtest's sine takes eight bits of its phase: its steps are
        // up to a fifth or so uneven.  A break is a step of the order of
        // the whole amplitude.)
        let slope = (1.4 * 2.0 * std::f64::consts::PI * f64::from(hz) / 44_100.0 * f64::from(peak)) as i32;
        assert!(peak > 0 && largest <= slope + 2, "{}: {} Hz: a step of {} where its sine of {} has at most {}", name, hz, largest, peak, slope);
    }
    assert!(!text.contains("unexpected"), "{}: {:?}", name, tail);
    // And back to the SD loader, with the flash as it was.
    let at = text.find("DONE.").unwrap();
    until(&mut m, at, "TEST RETURNED: 0", 60).unwrap_or_else(|| panic!("{}: not back at the loader: {:?}", name, String::from_utf8_lossy(&m.console[at..])));
    assert_eq!(m.board.flash, flash, "{}", name);
    let cache = &m.cpu.cache.trouble;
    assert_eq!((cache.stale_fetches, cache.dirty_discarded), (0, 0), "{}: {:?}", name, cache);
}

/// RETURN held for two seconds (CLAUDE.md, "Mandatory hardware
/// behavior"): in a program the SD loader started, back to the loader
/// (and a RETURN still held then is not taken for a new one); in the
/// loader, back to the stock software: the display opens, the disc is
/// ejected, and the unit starts anew, its flash untouched.
#[test]
fn return_held_goes_back_to_the_loader_and_from_there_to_stock() {
    use rnse::board::Model;
    let Some(image) = disc() else { return };
    let sdk = std::path::Path::new(env!("CARGO_MANIFEST_DIR")).to_path_buf();
    let Some((mut m, name, _)) = started_by_stock_software(&image, Model::Rnse192, "sdboot") else { return };
    assert!(sdk_example(&image, Model::Rnse192, "returntest2").is_some());
    let second = std::fs::read(sdk.join("out/returntest2-192.bin")).unwrap();
    let flash = m.board.flash.clone();
    let ms = m.cycles_per_ms();
    m.cpu.breakpoints.clear();
    assert_eq!(m.run(2000 * ms), Stop::Budget);
    let card = rnse::fat::card("HOLD", &[rnse::fat::Entry::file("SECOND.BIN", second)], None).unwrap();
    let at = m.console.len();
    assert!(m.insert_card(0, rnse::dev::sdhi::Card::from_bytes(card).unwrap()));
    // (Within 100 ms of its start: its five seconds are already running.)
    let started = until_soon(&mut m, at, "rnse sdk: returntest2", 30).unwrap_or_else(|| panic!("{}: {}", name, String::from_utf8_lossy(&m.console[at..])));
    // Held within the program's five seconds: the loader again, before
    // the program would have returned by itself (status 23).  Pressed
    // once it has looked at the keys (a RETURN already down when a
    // program starts counts only after it has been let go).
    assert_eq!(m.run(300 * ms), Stop::Budget);
    assert!(m.panel_key("RETURN", true));
    assert_eq!(m.run(2600 * ms), Stop::Budget);
    let text = String::from_utf8_lossy(&m.console[started..]).into_owned();
    assert!(text.contains("TEST ENDED BY HOLDING RETURN"), "{}: {}", name, text);
    assert!(!text.contains("TEST RETURNED: 23"), "{}: {}", name, text);
    // Still held, and for longer than two seconds: not the loader's own
    // hold, which would leave for the stock software.
    assert_eq!(m.run(2500 * ms), Stop::Budget);
    assert!(m.panel_key("RETURN", false));
    assert_eq!(m.run(1500 * ms), Stop::Budget);
    assert!(!String::from_utf8_lossy(&m.console[started..]).contains("BACK TO THE STOCK SOFTWARE"), "{}", name);
    assert!(m.atapi().image.is_some(), "{}: the disc is still in", name);
    // A new hold in the loader: display open, disc out, a new start.
    let at = m.console.len();
    assert!(m.panel_key("RETURN", true));
    let stop = m.run(2600 * ms);
    assert!(m.panel_key("RETURN", false));
    let stop = if stop == Stop::Budget { m.run(15_000 * ms) } else { stop };
    let text = String::from_utf8_lossy(&m.console[at..]).into_owned();
    assert!(text.contains("BACK TO THE STOCK SOFTWARE"), "{}: {}", name, text);
    assert!(text.contains("EJECTING THE DISC"), "{}: {}", name, text);
    assert!(m.board.display_pos <= 94, "{}: the display is not open: {}", name, m.board.display_pos);
    assert!(m.atapi().image.is_none(), "{}: the disc was not ejected", name);
    assert!(stop != Stop::Budget && m.reset_requested, "{}: no new start: {:?} {:?}", name, stop, m.cpu.fault);
    assert_eq!(m.board.flash, flash, "{}", name);
    let cache = &m.cpu.cache.trouble;
    assert_eq!((cache.stale_fetches, cache.dirty_discarded), (0, 0), "{}: {:?}", name, cache);
}

/// The radio processor's keep-alive: a unit left in the SD loader's menu
/// (and under Linux) was restarted into the stock software after about
/// seven minutes, the stock software's 81 [06] of every minute not
/// coming.  The loader sends it now (panel_service, radio_keepalive);
/// with the emulated radio processor's patience cut to 70 s, the menu is
/// still there after 150 s, the keep-alives coming once a minute.
#[test]
fn the_loader_keeps_the_radio_processor_content() {
    use rnse::board::Model;
    let Some(image) = disc() else { return };
    let Some((mut m, name, _)) = started_by_stock_software(&image, Model::Rnse192, "sdboot") else { return };
    m.radio_keepalive_limit_ms = 70_000;
    let ms = m.cycles_per_ms();
    m.cpu.breakpoints.clear();
    let before = m.radio.seen.get(&(0x81, 0x06)).copied().unwrap_or(0);
    assert_eq!(m.run(150_000 * ms), Stop::Budget, "{}: {:?}", name, m.cpu.fault);
    assert!(!m.reset_requested, "{}: the unit was restarted", name);
    let sent = m.radio.seen.get(&(0x81, 0x06)).copied().unwrap_or(0) - before;
    assert!((2..=4).contains(&sent), "{}: {} keep-alives in 150 s", name, sent);
    assert!(String::from_utf8_lossy(&m.console).contains("WAITING FOR CARD"), "{}", name);
}

/// Programs from an SD card (examples/sdboot): the stock software
/// starts the loader from the disc, the loader starts the card
/// processor of a slot with the installed software's program out of
/// the flash, reads RNSE.BIN from the card's FAT and starts it in its
/// own place; and with the card in the second slot likewise.
#[test]
fn programs_come_from_an_sd_card() {
    use rnse::board::Model;
    let Some(image) = disc() else { return };
    let sdk = std::path::Path::new(env!("CARGO_MANIFEST_DIR")).to_path_buf();
    for slot in [0, 1] {
        let Some((mut m, name, _)) = started_by_stock_software(&image, Model::Rnse192, "sdboot") else { return };
        // (Built by sdk_example for the two tests of it.)
        // The probe, because it draws and leaves the faceplate
        // controller alone: the screen's light is the controller's, and
        // a loader that starts the controller anew and then reads a
        // card instead of talking to it hands over a dark screen (as a
        // unit showed).
        if sdk_example(&image, Model::Rnse192, "probe").is_none() { return }
        let probe = std::fs::read(sdk.join("out/probe-192.bin")).unwrap();
        let entries = vec![rnse::fat::Entry::file("RNSE.BIN", probe)];
        let card = rnse::fat::card("RNSE", &entries, None).unwrap();
        // In the second slot an old card, of version 1 of the
        // specification: no answer to CMD8, and busy for a while; and
        // one that is not awake at the first start, which the driver
        // has to make as the stock driver does.
        m.board.media.old_cards = slot == 1;
        m.board.media.deaf_cards = if slot == 1 { 2 } else { 0 };
        assert!(m.insert_card(slot, rnse::dev::sdhi::Card::from_bytes(card).unwrap()));
        let ms = m.cycles_per_ms();
        m.cpu.breakpoints.clear();
        if let Some(address) = Machine::known_printf("V2220371") { m.hook_printf(address); }
        assert_eq!(m.run(40_000 * ms), Stop::Budget, "{}: {:?}", name, m.cpu.fault);
        let text = String::from_utf8_lossy(&m.console).into_owned();
        let tail = &text[text.len().saturating_sub(400)..];
        for d in &m.board.media.dsps {
            assert_eq!(d.fault, None, "{}, slot {}: {} stopped", name, slot, d.name);
        }
        assert!(text.contains("rnse sdk: sdboot"), "{}: {:?}", name, tail);
        assert!(text.contains("STARTING IT"), "{}, slot {}: {:?}", name, slot, tail);
        assert!(text.contains("rnse sdk: probe"), "{}, slot {}: {:?}", name, slot, tail);
        assert!(!text.contains("unexpected"), "{}: {:?}", name, tail);
        let (_, _, pixels) = m.render();
        assert!(pixels.iter().any(|&p| p != 0), "{}, slot {}: the screen is dark", name, slot);
        let cache = &m.cpu.cache.trouble;
        assert_eq!((cache.stale_fetches, cache.dirty_discarded), (0, 0), "{}: {:?}", name, cache);
    }
}

/// Three cooperative tests run with one disc launch and no flash writes.
#[test]
fn sd_loader_returns_and_rearms_on_reinsertion() {
    use rnse::board::Model;
    let Some(image) = disc() else { return };
    for slot in [0, 1] {
        let Some((mut m, _, _)) = started_by_stock_software(&image, Model::Rnse192, "sdboot") else { return };
        assert!(sdk_example(&image, Model::Rnse192, "returntest").is_some(), "returntest build failed");
        let bin = std::fs::read(std::path::Path::new(env!("CARGO_MANIFEST_DIR")).join("out/returntest-192.bin")).unwrap();
        assert!(sdk_example(&image, Model::Rnse192, "returnapi").is_some());
        let api = std::fs::read(std::path::Path::new(env!("CARGO_MANIFEST_DIR")).join("out/returnapi-192.bin")).unwrap();
        let cards: Vec<_> = [bin, api].into_iter().map(|b| rnse::fat::card("TEST", &[rnse::fat::Entry::file("RNSE.BIN", b)], None).unwrap()).collect();
        let flash = m.board.flash.clone();
        let ms = m.cycles_per_ms();
        let mut from = 0;
        assert_eq!(m.run(2000 * ms), Stop::Budget);
        assert!(String::from_utf8_lossy(&m.console).contains("WAITING FOR CARD"));
        for cycle in 0..3 {
            assert!(m.insert_card(slot, rnse::dev::sdhi::Card::from_bytes(cards[cycle % 2].clone()).unwrap()));
            let expected = if cycle % 2 == 0 { "TEST RETURNED: 17" } else { "TEST RETURNED: 23" };
            let _end = until(&mut m, from, expected, 60).unwrap_or_else(|| panic!("slot {} cycle {}: {}", slot, cycle, String::from_utf8_lossy(&m.console)));
            assert_eq!(m.run(1000 * ms), Stop::Budget);
            let text = String::from_utf8_lossy(&m.console);
            assert_eq!(text.matches("rnse sdk: returntest").count(), cycle + 1);
            assert_eq!(text.matches("rnse sdk: sdboot").count(), cycle + 2);
            assert!(!text[text.find("rnse sdk: sdboot").unwrap()..].contains("[fw]"));
            assert_eq!(m.board.flash, flash);
            assert!(!text.contains("unexpected"));
            assert_eq!(m.cpu.cache.trouble.stale_fetches, 0);
            assert_eq!(m.cpu.cache.trouble.dirty_discarded, 0);
            if cycle == 2 {
                let at = m.console.len();
                assert!(m.panel_key("RETURN", true));
                assert_eq!(m.run(200 * ms), Stop::Budget);
                assert!(m.panel_key("RETURN", false));
                until(&mut m, at, "TEST RETURNED: 17", 60).expect("RETURN reruns the card");
                assert_eq!(String::from_utf8_lossy(&m.console).matches("rnse sdk: returntest").count(), 4);
            }
            from = m.console.len();
            m.eject_card(slot);
            assert_eq!(m.run(500 * ms), Stop::Budget);
        }
        // Bad cards leave the loader available for the next insertion.
        for bad in [None, Some(vec![0; 32]), Some(vec![0; 0x1c001])] {
            let entries = bad.map(|b| vec![rnse::fat::Entry::file("RNSE.BIN", b)]).unwrap_or_default();
            let card = rnse::fat::card("BAD", &entries, None).unwrap();
            let at = m.console.len();
            assert!(m.insert_card(slot, rnse::dev::sdhi::Card::from_bytes(card).unwrap()));
            until(&mut m, at, "LOAD ERROR:", 60).expect("invalid card must not execute");
            assert_eq!(m.board.flash, flash);
            m.eject_card(slot);
            assert_eq!(m.run(500 * ms), Stop::Budget);
        }
        let at = m.console.len();
        assert!(m.insert_card(slot, rnse::dev::sdhi::Card::from_bytes(cards[0].clone()).unwrap()));
        assert_eq!(m.run(500 * ms), Stop::Budget);
        m.eject_card(slot);
        until(&mut m, at, "LOAD ERROR:", 60).expect("removal during initialization must fail");
        assert!(!String::from_utf8_lossy(&m.console[at..]).contains("rnse sdk: returntest"));
        assert_eq!(m.run(500 * ms), Stop::Budget);
        let at = m.console.len();
        for both in [0, 1] {
            assert!(m.insert_card(both, rnse::dev::sdhi::Card::from_bytes(cards[0].clone()).unwrap()));
        }
        let first = until(&mut m, at, "TEST RETURNED: 17", 60).expect("first simultaneous card");
        until(&mut m, first + 1, "TEST RETURNED: 17", 60).expect("second simultaneous card");
        assert_eq!(m.run(2000 * ms), Stop::Budget);
        let log = String::from_utf8_lossy(&m.console[at..]);
        assert_eq!(log.matches("rnse sdk: returntest").count(), 2, "cards must not alternate forever");
        assert!(log.find("SLOT 1").unwrap() < log.find("SLOT 2").unwrap());
        assert_eq!(m.board.flash, flash);
        assert_eq!(m.cpu.cache.trouble.stale_fetches, 0);
        assert_eq!(m.cpu.cache.trouble.dirty_discarded, 0);
    }
}

#[test]
fn second_sd_test_returns_status_23() {
    use rnse::board::Model;
    let Some(image) = disc() else { return };
    let Some((mut m, _, _)) = started_by_stock_software(&image, Model::Rnse192, "sdboot") else { return };
    assert!(sdk_example(&image, Model::Rnse192, "returntest2").is_some(), "second test must build");
    let bin = std::fs::read(std::path::Path::new(env!("CARGO_MANIFEST_DIR")).join("out/returntest2-192.bin")).unwrap();
    let card = rnse::fat::card("SECOND", &[rnse::fat::Entry::file("RNSE.BIN", bin)], None).unwrap();
    let flash = m.board.flash.clone();
    assert!(m.insert_card(0, rnse::dev::sdhi::Card::from_bytes(card).unwrap()));
    until(&mut m, 0, "TEST RETURNED: 23", 60).expect("second test must return directly");
    assert!(String::from_utf8_lossy(&m.console).contains("rnse sdk: returntest2"));
    assert_eq!(m.board.flash, flash);
    assert_eq!(m.cpu.cache.trouble.stale_fetches, 0);
    assert_eq!(m.cpu.cache.trouble.dirty_discarded, 0);
}

/// Resident shell: actual SH-4 app code, audio, persistence and malformed input.
#[test]
fn modern_resident_shell() {
    use rnse::board::Model;
    let Some(image) = disc() else { return };
    let (mut m, name, _) = sdk_example(&image, Model::Rnse192, "modern")
        .expect("modern test requires the SH-4 compiler");
    let variants = fw::load_variants(&image).unwrap();
    let v = variants.iter().find(|v| v.name == name).unwrap();
    m.install(v, &fw::IndexInfo::from_disc(&image, &name).unwrap());
    let sdk = std::path::Path::new(env!("CARGO_MANIFEST_DIR")).to_path_buf();
    assert!(std::process::Command::new("sh").arg(sdk.join("apps/build.sh")).arg("trip").status().unwrap().success());
    let good = std::fs::read(sdk.join("out/app-trip.RNA")).unwrap();
    let mut incompatible = good.clone();
    incompatible[7] = 99;
    let mut oversized = good.clone();
    oversized[16..20].copy_from_slice(&0x80001u32.to_be_bytes());
    let mut entry = good.clone();
    entry[28..32].copy_from_slice(&0xffffffffu32.to_be_bytes());
    let mut bad = good.clone();
    *bad.last_mut().unwrap() ^= 1;
    let entries = vec![
        rnse::fat::Entry::file("TRIP.RNA", good),
        rnse::fat::Entry::file("BAD.RNA", bad),
        rnse::fat::Entry::file("ABI.RNA", incompatible),
        rnse::fat::Entry::file("SIZE.RNA", oversized),
        rnse::fat::Entry::file("ENTRY.RNA", entry),
        rnse::fat::Entry::file("TONES.MP3", include_bytes!("data/tones.mp3").to_vec()),
    ];
    let card = rnse::fat::card("MODERN", &entries, None).unwrap();
    m.insert_card(0, rnse::dev::sdhi::Card::from_bytes(card).unwrap());
    let ms = m.cycles_per_ms();
    let run = |m: &mut Machine, n| assert_eq!(m.run(n * ms), Stop::Budget, "{:?} {}", m.cpu.fault, String::from_utf8_lossy(&m.console));
    let press = |m: &mut Machine, key| {
        assert!(m.panel_key(key, true)); run(m, 150);
        assert!(m.panel_key(key, false)); run(m, 150);
    };
    run(&mut m, 2000);
    assert!(String::from_utf8_lossy(&m.console).contains("modern: ready"));
    let shot = |m: &mut Machine, name| {
        let (w,h,rgb)=m.render();
        std::fs::create_dir_all("out/modern").unwrap();
        std::fs::write(format!("out/modern/{name}.png"), rnse::png::encode_rgb(w,h,&rgb)).unwrap();
        rgb
    };
    let radio = shot(&mut m, "radio");
    assert!(radio.chunks(3).any(|p| p[0]>100&&p[1]<50));
    m.panel_knob("volume", 2); run(&mut m, 200);
    assert_eq!(m.radio.volume, 10);
    // The card started as the stock driver starts it (sd_init: a ready
    // card's silence to CMD55, then a new start, a step every 150 ms):
    // some 8 to 13 s here.
    let at = m.console.len();
    press(&mut m, "MEDIA"); press(&mut m, "SOFT_BL");
    assert!(until_soon(&mut m, at, "modern: card ready", 20).is_some(), "{}", String::from_utf8_lossy(&m.console[at..]));
    shot(&mut m, "media");
    press(&mut m, "SELECT"); run(&mut m, 2000);
    assert!(m.board.media.dsps[0].loud_frames > 0);
    press(&mut m, "INFO"); press(&mut m, "SELECT");
    assert!(String::from_utf8_lossy(&m.console).contains("modern: app started"));
    let before=shot(&mut m, "app");
    m.panel_knob("main", 3); run(&mut m, 200);
    assert_ne!(before, shot(&mut m, "app-adjusted"));
    press(&mut m, "RETURN");
    assert!(String::from_utf8_lossy(&m.console).contains("modern: app exited"));
    m.panel_knob("main", 1); run(&mut m, 200);
    let starts=String::from_utf8_lossy(&m.console).matches("modern: app started").count();
    press(&mut m, "SELECT");
    assert_eq!(starts, String::from_utf8_lossy(&m.console).matches("modern: app started").count());
    shot(&mut m, "bad-app");
    for _ in 0..3 {
        m.panel_knob("main", 1); run(&mut m, 200);
        press(&mut m, "SELECT");
        assert_eq!(starts, String::from_utf8_lossy(&m.console).matches("modern: app started").count());
    }
    // Empty second slot is recoverable through physical controls.
    press(&mut m, "SOFT_TR");
    run(&mut m, 8000);
    shot(&mut m, "no-card");
    press(&mut m, "RADIO");
    assert_eq!(m.radio.source, rnse::dev::radio::Radio::SOURCE_TUNER);
    let saved=m.board.nvram.clone();
    let (mut reboot,_,_)=sdk_example(&image, Model::Rnse192, "modern").unwrap();
    reboot.install(v, &fw::IndexInfo::from_disc(&image, &name).unwrap());
    reboot.board.nvram.copy_from_slice(&saved);
    run(&mut reboot,2000);
    assert_eq!(reboot.radio.volume,10);
    let (mut invalid,_,_)=sdk_example(&image, Model::Rnse192, "modern").unwrap();
    invalid.install(v, &fw::IndexInfo::from_disc(&image, &name).unwrap());
    invalid.board.nvram.fill(0xa5);
    run(&mut invalid,2000);
    assert_eq!(invalid.radio.volume,8, "corrupt settings restore defaults");
    assert_eq!(m.resets,0);
}

#[test]
fn modern_installed_recovery() {
    let (Some(image), Some(rom)) = (disc(), boot_rom()) else { return };
    let custom = firmware_disc(&image, "modern").expect("SH-4 toolchain required");
    let mut m = rom_192(&image, &custom, &rom).unwrap();
    until(&mut m,0,"modern: ready",600).expect("stock loader must install and boot modern");
    let ms=m.cycles_per_ms();
    assert_eq!(m.run(12_000*ms),Stop::Budget);
    assert_eq!(m.resets,0);
    assert_eq!(&m.board.nvram[0x3ff04..0x3ff08], &[0,0,0,0], "boot guard healthy");
    m.atapi().load(image.clone());
    for key in ["SETUP", "SELECT"] {
        if key=="SELECT" {m.panel_knob("main",1);m.run(200*ms);}
        m.panel_key(key,true);m.run(200*ms);m.panel_key(key,false);m.run(200*ms);
    }
    // RETURN from recovery cancels, never erases.
    let directory=m.board.flash[0xc000..0xe000].to_vec();
    m.panel_key("RETURN",true);m.run(200*ms);m.panel_key("RETURN",false);m.run(200*ms);
    assert_eq!(directory,m.board.flash[0xc000..0xe000]);
    m.panel_knob("main",1);m.run(200*ms);
    m.panel_key("SELECT",true);m.run(200*ms);m.panel_key("SELECT",false);m.run(200*ms);
    m.panel_key("SELECT",true);m.run(3000*ms);m.panel_key("SELECT",false);
    until(&mut m,0,"[fw] SOUND_TSK START",600).expect("stock firmware restored");
    stock_is_in_flash(&m,&image);
}

/// Built-ins exercise real firmware editing, SRAM and factory CAN transport.
/// The peer below is a protocol fixture, not real Audi-module compatibility evidence.
#[test]
fn modern_builtins() {
    use rnse::board::Model;
    use rnse::dev::{hcan::Frame, tp20::Peer};
    let Some(image) = disc() else { return };
    let (mut m, name, _) = sdk_example(&image, Model::Rnse192, "modern")
        .expect("modern built-in test requires SH-4 compiler");
    let variants = fw::load_variants(&image).unwrap();
    let v = variants.iter().find(|v| v.name == name).unwrap();
    m.install(v, &fw::IndexInfo::from_disc(&image, &name).unwrap());
    let ms=m.cycles_per_ms();
    let run=|m:&mut Machine,n|assert_eq!(m.run(n*ms),Stop::Budget,"{:?}",m.cpu.fault);
    let press=|m:&mut Machine,key|{m.panel_key(key,true);run(m,150);m.panel_key(key,false);run(m,150);};
    let turn=|m:&mut Machine,n|{m.panel_knob("main",n);run(m,100);};
    let shot=|m:&mut Machine,name|{let(w,h,rgb)=m.render();std::fs::create_dir_all("out/modern").unwrap();std::fs::write(format!("out/modern/{name}.png"),rnse::png::encode_rgb(w,h,&rgb)).unwrap();rgb};
    run(&mut m,2000);
    press(&mut m,"CAR");
    let unknown=shot(&mut m,"vehicle-no-data");
    m.can_send_cyclic(Frame::new(0x271,&[3]),100);
    run(&mut m,1200);
    assert!(unknown != shot(&mut m,"vehicle"), "vehicle data must refresh");
    // Six persistent local contacts, empty by default. Write AB / 01 through rotary editor.
    press(&mut m,"NAME");press(&mut m,"SELECT");press(&mut m,"SOFT_TL");
    turn(&mut m,1);press(&mut m,"SELECT");turn(&mut m,1);press(&mut m,"SELECT");press(&mut m,"SOFT_BR");
    press(&mut m,"SOFT_TR");press(&mut m,"SELECT");turn(&mut m,1);press(&mut m,"SELECT");press(&mut m,"SOFT_BR");
    shot(&mut m,"contact");
    let latest=|m:&Machine| {let a=&m.board.nvram[0x3fc00..0x3fd80];let b=&m.board.nvram[0x3fd80..0x3ff00];if u32::from_be_bytes(a[4..8].try_into().unwrap())>u32::from_be_bytes(b[4..8].try_into().unwrap()){a.to_vec()}else{b.to_vec()}};
    let book=latest(&m);
    assert_eq!(&book[8..11],b"AB\0");assert_eq!(&book[28..31],b"01\0");
    // Cancel leaves the original name. Delete requires explicit soft-key confirmation.
    press(&mut m,"SOFT_TL");press(&mut m,"SOFT_TR");press(&mut m,"RETURN");
    assert_eq!(book,latest(&m));
    press(&mut m,"SOFT_BL");press(&mut m,"RETURN");assert_eq!(book,latest(&m));
    // Saved latitude and longitude, with bounds checked before saving.
    let enter=|m:&mut Machine,text:&str| {let chars="0123456789-.";let mut at=0;press(m,"SOFT_TR");for c in text.chars(){let next=chars.find(c).unwrap() as i32;turn(m,next-at);press(m,"SELECT");at=next;}press(m,"SOFT_BR");};
    press(&mut m,"NAV");press(&mut m,"SOFT_BR");press(&mut m,"SOFT_TR");
    enter(&mut m,"90.0001");assert_eq!(book,latest(&m));
    shot(&mut m,"invalid-coordinate");
    press(&mut m,"RETURN");press(&mut m,"SOFT_TR");enter(&mut m,"37.7749");
    press(&mut m,"SOFT_BR");enter(&mut m,"-122.4194");
    let location=latest(&m);
    assert_eq!(i32::from_be_bytes(location[268..272].try_into().unwrap()),377749);
    assert_eq!(i32::from_be_bytes(location[272..276].try_into().unwrap()),-1224194);
    shot(&mut m,"destination");
    // Original fixed CAN IDs, acknowledged segmented commands and actual module replies.
    m.can_peers.push(Peer::new(0x6c4,0x6c5));
    let mut handled=0usize;
    let mut call=-1i32;
    let pump=|m:&mut Machine,handled:&mut usize,call:&mut i32,n:u64|{
        for _ in 0..n/100 {
            run(m,100);
            let blocks=m.can_peers[0].received[*handled..].to_vec();
            *handled+=blocks.len();
            for block in blocks {
                let cmd=String::from_utf8(block).unwrap();
                let response=if cmd=="AT\r" {"\r\nOK\r\n".to_string()}
                    else if cmd=="AT+CLCC\r" {if *call<0 {"\r\nOK\r\n".to_string()} else {format!("\r\n+CLCC: 1,0,{},0,0,\"01\",129\r\nOK\r\n",*call)}}
                    else if cmd=="ATD01;\r" {*call=2;"\r\nOK\r\n".to_string()}
                    else if cmd=="ATA\r" {*call=0;"\r\nOK\r\n".to_string()}
                    else if cmd=="AT+CHUP\r" {*call=-1;"\r\nOK\r\n".to_string()}
                    else {panic!("unexpected telephone command {cmd:?}")};
                let frames=m.can_peers[0].send(response.as_bytes());
                for f in frames {m.can_frame(f);}
            }
        }
    };
    press(&mut m,"TEL");press(&mut m,"SOFT_TL");pump(&mut m,&mut handled,&mut call,800);
    assert!(m.can_peers[0].open);
    assert!(m.can_peers[0].received.iter().any(|c|c==b"AT\r"));
    press(&mut m,"NAME");press(&mut m,"SELECT");press(&mut m,"SOFT_BR");
    pump(&mut m,&mut handled,&mut call,500);
    // Let the current query finish, then make one explicit request.
    press(&mut m,"SOFT_BR");pump(&mut m,&mut handled,&mut call,1000);
    assert!(m.can_peers[0].received.iter().any(|c|c==b"ATD01;\r"));
    assert_eq!(call,2);
    shot(&mut m,"telephone-dialling");
    call=0;pump(&mut m,&mut handled,&mut call,1500);shot(&mut m,"telephone-active");
    pump(&mut m,&mut handled,&mut call,200);press(&mut m,"SOFT_BL");pump(&mut m,&mut handled,&mut call,1500);
    assert!(m.can_peers[0].received.iter().any(|c|c==b"AT+CHUP\r"));
    // Incoming call and answer use replies, rather than local connected flags.
    call=4;pump(&mut m,&mut handled,&mut call,1500);shot(&mut m,"telephone-ringing");
    press(&mut m,"SOFT_BR");pump(&mut m,&mut handled,&mut call,1500);
    assert!(m.can_peers[0].received.iter().any(|c|c==b"ATA\r"));
    assert_eq!(call,0);
    // Fresh boot restores notebook independently of shell settings and guard.
    let saved=m.board.nvram.clone();
    let(mut reboot,_,_)=sdk_example(&image,Model::Rnse192,"modern").unwrap();
    reboot.board.nvram.copy_from_slice(&saved);run(&mut reboot,2000);
    press(&mut reboot,"NAME");press(&mut reboot,"SELECT");shot(&mut reboot,"contact-restored");
    assert_eq!(latest(&reboot),location);
    assert_eq!(m.resets,0);
}
