#![no_std]
#![no_main]
#![cfg_attr(test, feature(custom_test_frameworks))]
#![cfg_attr(test, test_runner(dunit::testing::runner))]
#![cfg_attr(test, reexport_test_harness_main = "__dunit_test_main")]

extern crate alloc;

use core::alloc::Layout;
use dunit::time::{Duration, Instant};
use dunit::window::{
    Window, WindowEvent, IN_DOWN, IN_FOCUS_IN, IN_FOCUS_OUT, IN_KEY, IN_KEY_UP, IN_LEAVE, IN_MOVE,
    IN_POINTER_MODE, IN_QUIT, IN_RELATIVE, IN_SCROLL, IN_UP, PointerMode,
};

#[repr(C, align(16))]
struct MallocHeader {
    size: usize,
    _pad: usize,
}

#[no_mangle]
pub unsafe extern "C" fn malloc(size: usize) -> *mut u8 {
    if size == 0 {
        return core::ptr::null_mut();
    }
    let total = match size.checked_add(16) {
        Some(t) => t,
        None => return core::ptr::null_mut(),
    };
    let layout = match Layout::from_size_align(total, 16) {
        Ok(l) => l,
        Err(_) => return core::ptr::null_mut(),
    };
    let ptr = alloc::alloc::alloc(layout);
    if ptr.is_null() {
        return core::ptr::null_mut();
    }
    *(ptr as *mut MallocHeader) = MallocHeader { size, _pad: 0 };
    ptr.add(16)
}

#[no_mangle]
pub unsafe extern "C" fn free(ptr: *mut u8) {
    if ptr.is_null() {
        return;
    }
    let raw = ptr.sub(16);
    let header = &*(raw as *const MallocHeader);
    let total = header.size + 16;
    if let Ok(layout) = Layout::from_size_align(total, 16) {
        alloc::alloc::dealloc(raw, layout);
    }
}

#[no_mangle]
pub unsafe extern "C" fn calloc(nmemb: usize, size: usize) -> *mut u8 {
    let total = match nmemb.checked_mul(size) {
        Some(t) => t,
        None => return core::ptr::null_mut(),
    };
    let ptr = malloc(total);
    if !ptr.is_null() {
        core::ptr::write_bytes(ptr, 0, total);
    }
    ptr
}

#[no_mangle]
pub unsafe extern "C" fn realloc(ptr: *mut u8, new_size: usize) -> *mut u8 {
    if ptr.is_null() {
        return malloc(new_size);
    }
    if new_size == 0 {
        free(ptr);
        return core::ptr::null_mut();
    }
    let raw = ptr.sub(16);
    let header = &*(raw as *const MallocHeader);
    let old_size = header.size;
    let new_ptr = malloc(new_size);
    if !new_ptr.is_null() {
        let copy_len = old_size.min(new_size);
        core::ptr::copy_nonoverlapping(ptr, new_ptr, copy_len);
        free(ptr);
    }
    new_ptr
}

static ATLAS_PNG: &[u8] = include_bytes!("../../assets/generated/world_atlas.png");
static ATLAS_TXT: &[u8] = include_bytes!("../../assets/generated/world_atlas.txt");
static FONT_ALPHA: &[u8] = include_bytes!("../../assets/generated/font_ascii.alpha");

enum McedDunitOpaque {}

#[allow(dead_code)]
extern "C" {
    fn mced_dunit_create_embedded(
        width: i32,
        height: i32,
        atlas_png: *const u8,
        atlas_png_len: usize,
        atlas_txt: *const u8,
        atlas_txt_len: usize,
        font_alpha: *const u8,
        font_alpha_len: usize,
    ) -> *mut McedDunitOpaque;

    fn mced_dunit_destroy(d: *mut McedDunitOpaque);
    fn mced_dunit_update(d: *mut McedDunitOpaque, dt: f32);
    fn mced_dunit_resize(d: *mut McedDunitOpaque, width: i32, height: i32);
    fn mced_dunit_render(d: *mut McedDunitOpaque) -> *const u32;

    fn mced_dunit_key(d: *mut McedDunitOpaque, scancode: i32, key_char: i32, down: bool);
    fn mced_dunit_mouse_move(d: *mut McedDunitOpaque, x: i32, y: i32);
    fn mced_dunit_mouse_leave(d: *mut McedDunitOpaque);
    fn mced_dunit_mouse_look(d: *mut McedDunitOpaque, dx: f32, dy: f32);
    fn mced_dunit_mouse_button(d: *mut McedDunitOpaque, button: i32, down: bool);
    fn mced_dunit_mouse_scroll(d: *mut McedDunitOpaque, delta: i32);
    fn mced_dunit_start_world(d: *mut McedDunitOpaque, new_world: bool);
    fn mced_dunit_is_in_world(d: *mut McedDunitOpaque) -> bool;
}

const WIDTH: u32 = 640;
const HEIGHT: u32 = 480;

#[dunit::main]
fn main() -> dunit::Result<()> {
    dunit::println!("[MCED] Starting Minecraft C Edition on Dunit OS...")?;

    let mut window = Window::open(WIDTH, HEIGHT)?;
    let (mut cur_w, mut cur_h) = window.size();

    let game = unsafe {
        mced_dunit_create_embedded(
            cur_w as i32,
            cur_h as i32,
            ATLAS_PNG.as_ptr(),
            ATLAS_PNG.len(),
            ATLAS_TXT.as_ptr(),
            ATLAS_TXT.len(),
            FONT_ALPHA.as_ptr(),
            FONT_ALPHA.len(),
        )
    };

    if game.is_null() {
        dunit::println!("[MCED] Failed to initialize game engine!")?;
        return Err(dunit::Error(libdunit::ENOMEM));
    }

    dunit::println!("[MCED] Game engine initialized successfully!")?;

    let mut last_time = Instant::now()?;
    let mut running = true;
    let mut is_focused = true;
    let mut current_mode = PointerMode::Normal;

    while running {
        // Drain events
        loop {
            match window.next_event(Duration::from_millis(0))? {
                Some(WindowEvent::CloseRequested) => {
                    running = false;
                    break;
                }
                Some(WindowEvent::Resized { width, height }) => {
                    cur_w = width;
                    cur_h = height;
                    unsafe {
                        mced_dunit_resize(game, width as i32, height as i32);
                    }
                }
                Some(WindowEvent::Input(input)) => match input.kind {
                    IN_FOCUS_IN => {
                        is_focused = true;
                        if unsafe { mced_dunit_is_in_world(game) } {
                            let _ = window.request_pointer_mode(PointerMode::Locked);
                        }
                    }
                    IN_FOCUS_OUT => {
                        is_focused = false;
                        let _ = window.request_pointer_mode(PointerMode::Normal);
                        current_mode = PointerMode::Normal;
                    }
                    IN_POINTER_MODE => {
                        if input.y == 0 {
                            if let Some(mode) = PointerMode::from_u32(input.value as u32) {
                                current_mode = mode;
                            }
                        } else {
                            current_mode = PointerMode::Normal;
                        }
                    }
                    IN_RELATIVE => {
                        unsafe {
                            mced_dunit_mouse_look(game, input.x as f32, input.y as f32);
                        }
                    }
                    IN_KEY => {
                        let scancode = input.x;
                        let key_char = input.value as i32;
                        unsafe {
                            mced_dunit_key(game, scancode, key_char, true);
                        }
                    }
                    IN_KEY_UP => {
                        let scancode = input.x;
                        let key_char = input.value as i32;
                        unsafe {
                            mced_dunit_key(game, scancode, key_char, false);
                        }
                    }
                    IN_MOVE => {
                        if current_mode != PointerMode::Locked {
                            unsafe {
                                mced_dunit_mouse_move(game, input.x, input.y);
                            }
                        }
                    }
                    IN_LEAVE => {
                        unsafe {
                            mced_dunit_mouse_leave(game);
                        }
                    }
                    IN_DOWN => {
                        if is_focused && unsafe { mced_dunit_is_in_world(game) } && current_mode != PointerMode::Locked {
                            if window.request_pointer_mode(PointerMode::Locked).is_ok() {
                                current_mode = PointerMode::Locked;
                            }
                        }
                        unsafe {
                            mced_dunit_mouse_button(game, input.value as i32, true);
                        }
                    }
                    IN_UP => {
                        unsafe {
                            mced_dunit_mouse_button(game, input.value as i32, false);
                        }
                    }
                    IN_SCROLL => {
                        unsafe {
                            mced_dunit_mouse_scroll(game, input.x);
                        }
                    }
                    IN_QUIT => {
                        running = false;
                        break;
                    }
                    _ => {}
                },
                _ => break,
            }
        }

        // Pointer mode synchronization based on game state
        let in_world = unsafe { mced_dunit_is_in_world(game) };
        let desired_mode = if in_world && is_focused {
            PointerMode::Locked
        } else {
            PointerMode::Normal
        };
        if desired_mode != current_mode {
            if window.request_pointer_mode(desired_mode).is_ok() {
                current_mode = desired_mode;
            }
        }

        // Time delta
        let elapsed = last_time.elapsed().unwrap_or(Duration::from_millis(16));
        let ms = elapsed.as_millis().clamp(1, 100);
        let dt = (ms as f32) / 1000.0f32;
        last_time = Instant::now()?;

        // Game update
        unsafe {
            mced_dunit_update(game, dt);
        }

        // Game render and present
        window.draw(|frame| {
            let (fw, fh) = frame.size();
            if fw != cur_w || fh != cur_h {
                cur_w = fw;
                cur_h = fh;
                unsafe {
                    mced_dunit_resize(game, fw as i32, fh as i32);
                }
            }
            let pixels_ptr = unsafe { mced_dunit_render(game) };
            if !pixels_ptr.is_null() {
                let slice = unsafe {
                    core::slice::from_raw_parts(pixels_ptr, (fw * fh) as usize)
                };
                let fpixels = frame.pixels_mut();
                if fpixels.len() == slice.len() {
                    fpixels.copy_from_slice(slice);
                }
            }
        })?;
    }

    unsafe {
        mced_dunit_destroy(game);
    }
    window.close()
}

#[dunit::test]
fn test_mced_init() -> dunit::Result<()> {
    let game = unsafe {
        mced_dunit_create_embedded(
            64,
            64,
            ATLAS_PNG.as_ptr(),
            ATLAS_PNG.len(),
            ATLAS_TXT.as_ptr(),
            ATLAS_TXT.len(),
            FONT_ALPHA.as_ptr(),
            FONT_ALPHA.len(),
        )
    };
    if game.is_null() {
        return Err(dunit::Error(libdunit::ENOMEM));
    }
    unsafe {
        mced_dunit_update(game, 0.016);
        let ptr = mced_dunit_render(game);
        if ptr.is_null() {
            mced_dunit_destroy(game);
            return Err(dunit::Error(libdunit::EFAULT));
        }
        mced_dunit_destroy(game);
    }
    Ok(())
}

#[dunit::test]
fn test_mced_block_place_break() -> dunit::Result<()> {
    let game = unsafe {
        mced_dunit_create_embedded(
            64,
            64,
            ATLAS_PNG.as_ptr(),
            ATLAS_PNG.len(),
            ATLAS_TXT.as_ptr(),
            ATLAS_TXT.len(),
            FONT_ALPHA.as_ptr(),
            FONT_ALPHA.len(),
        )
    };
    if game.is_null() {
        return Err(dunit::Error(libdunit::ENOMEM));
    }
    unsafe {
        mced_dunit_start_world(game, true);
        mced_dunit_update(game, 0.05);

        // Simulate placing a block (right click down, update, up)
        mced_dunit_mouse_button(game, 1, true);
        mced_dunit_update(game, 0.05);
        mced_dunit_mouse_button(game, 1, false);
        mced_dunit_update(game, 0.05);

        // Simulate breaking a block (left click down, update, up)
        mced_dunit_mouse_button(game, 0, true);
        mced_dunit_update(game, 0.05);
        mced_dunit_mouse_button(game, 0, false);
        mced_dunit_update(game, 0.05);

        // Render frame to ensure particles and dirty chunk meshing work
        let ptr = mced_dunit_render(game);
        if ptr.is_null() {
            mced_dunit_destroy(game);
            return Err(dunit::Error(libdunit::EFAULT));
        }
        mced_dunit_destroy(game);
    }
    Ok(())
}
