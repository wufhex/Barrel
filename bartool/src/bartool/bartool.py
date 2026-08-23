import sys
import os
import argparse
import ctypes
import re
from typing import Union, List, Tuple

from rich.console import Console
from rich.table import Table
from rich.panel import Panel
from rich.text import Text
from rich import box

from pybarrel import (
    Archive,
    BarrelError,
    Error,
    EntryFlags,
    BRL_DiskHeader,
    BRL_EntryMeta,
    HeaderFlags
)

console = Console()
error_console = Console(stderr=True)

def parse_size(size_str: str) -> int:
    """Parses unit string formats (e.g., '512', '7B', '100KB', '2MB', '1GiB') into raw byte integers."""
    if isinstance(size_str, int):
        return size_str
    
    size_str = size_str.strip()
    if size_str.isdigit():
        return int(size_str)

    units = {
        "b": 1,
        "k": 1024,
        "kb": 1024,
        "kib": 1024,
        "m": 1024**2,
        "mb": 1024**2,
        "mib": 1024**2,
        "g": 1024**3,
        "gb": 1024**3,
        "gib": 1024**3,
        "t": 1024**4,
        "tb": 1024**4,
        "tib": 1024**4,
    }

    match = re.match(r"^(\d+(?:\.\d+)?)\s*([a-zA-Z]+)$", size_str)
    if not match:
        raise argparse.ArgumentTypeError(f"Invalid size expression: '{size_str}'")

    val_str, unit_str = match.groups()
    unit = unit_str.lower()
    
    if unit not in units:
        raise argparse.ArgumentTypeError(f"Unknown size unit: '{unit_str}'. Supported: B, KB, MB, GB, TB (and IEC equivalents)")

    return int(float(val_str) * units[unit])

def format_bytes(size: int) -> str:
    """Formats byte counts into human-readable units."""
    for unit in ["B", "KiB", "MiB", "GiB", "TiB"]:
        if abs(size) < 1024.0:
            return f"{size:3.1f} {unit}"
        size /= 1024.0
    return f"{size:.1f} PiB"

def hex_dump(data: bytes, width: int = 16) -> None:
    """Prints standard hex dump with ASCII sidebar using Rich styling."""
    lines = []
    for i in range(0, len(data), width):
        chunk = data[i : i + width]
        hex_bytes = " ".join(f"[cyan]{b:02x}[/cyan]" for b in chunk)
        ascii_chars = "".join(
            f"[green]{chr(b)}[/green]" if 32 <= b <= 126 else "[dim].[/dim]"
            for b in chunk
        )
        
        # Padding for last row alignment
        padding = " " * ((width - len(chunk)) * 3)
        lines.append(f"[bold yellow]{i:08x}:[/bold yellow]  {hex_bytes}{padding}  |{ascii_chars}|")

    dump_text = "\n".join(lines)
    console.print(Panel(dump_text, title="Payload Hex View", expand=False, border_style="dim"))

def parse_bytes_input(s: str) -> bytes:
    """Parses hex formats like '0xAA0xBB', '0xAA,0xBB', '\\xAA\\xBB', or 'AABB' into bytes."""
    cleaned = s.replace("\\x", "").replace("0x", "").replace(",", "").replace(" ", "")
    try:
        return bytes.fromhex(cleaned)
    except ValueError as e:
        raise argparse.ArgumentTypeError(f"Invalid byte/hex payload: '{s}'") from e

def parse_key(key_str: str) -> Union[str, int]:
    """Parses string key or hex/decimal hash integer."""
    try:
        if key_str.lower().startswith("0x"):
            return int(key_str, 16)
        if key_str.isdigit():
            return int(key_str)
    except ValueError:
        pass
    return key_str

def read_index_slots(filepath: str) -> Tuple[BRL_DiskHeader, List[Tuple[int, BRL_EntryMeta]]]:
    """Reads disk header and parses Structure of Arrays (SoA) hashes and metadata."""
    with open(filepath, "rb") as f:
        raw_hdr = f.read(ctypes.sizeof(BRL_DiskHeader))
        if len(raw_hdr) < ctypes.sizeof(BRL_DiskHeader):
            raise ValueError("File too small to contain valid BRL_DiskHeader")

        hdr = BRL_DiskHeader.from_buffer_copy(raw_hdr)

        hashes_size = hdr.index_capacity * 8  # uint64_t * index_capacity
        meta_size = ctypes.sizeof(BRL_EntryMeta)  # 40 bytes

        # Read Hashes Array at index_offset
        f.seek(hdr.index_offset)
        raw_hashes = f.read(hashes_size)
        hashes = (ctypes.c_uint64 * hdr.index_capacity).from_buffer_copy(raw_hashes)

        # Read Metadata Array immediately following Hashes Array
        meta_offset = hdr.index_offset + hashes_size
        f.seek(meta_offset)
        raw_metadata = f.read(hdr.index_capacity * meta_size)

        active_entries: List[Tuple[int, BRL_EntryMeta]] = []

        # BRL_EMPTY_HASH = 0, BRL_TOMBSTONE_HASH = 1, BRL_VALID_HASH >= 2
        for i in range(hdr.index_capacity):
            entry_hash = hashes[i]
            meta = BRL_EntryMeta.from_buffer_copy(
                raw_metadata[i * meta_size : (i + 1) * meta_size]
            )

            if entry_hash >= 2 and (meta.flags & EntryFlags.ACTIVE):
                active_entries.append((entry_hash, meta))

        return hdr, active_entries

def add_archive_arg(parser: argparse.ArgumentParser, flag_aliases=("-i", "--input")) -> None:
    """Adds a positional archive path argument with backwards-compatible option flags."""
    parser.add_argument("archive_pos", nargs="?", help="Path to the Barrel archive file")
    if flag_aliases:
        parser.add_argument(*flag_aliases, dest="archive_opt", help="Path to the Barrel archive file (flag alternative)")

def get_archive_path(args: argparse.Namespace) -> str:
    """Resolves archive path from positional argument or flag fallback."""
    path = args.archive_pos or getattr(args, "archive_opt", None)
    if not path:
        raise ValueError("Archive filepath is required. Pass it directly or via flag options.")
    return path

def cmd_create(args: argparse.Namespace) -> None:
    filepath = get_archive_path(args)
    Archive.create(
        filepath=filepath,
        hints=args.hints,
        initial_capacity=args.init_cap,
        max_virtual_capacity=args.max_cap,
    )
    console.print(f"[green]✔[/green] Successfully created Barrel archive: [bold cyan]{filepath}[/bold cyan] (Max Cap: [bold]{format_bytes(args.max_cap)}[/bold])")

def cmd_read(args: argparse.Namespace) -> None:
    filepath = get_archive_path(args)
    key = parse_key(args.name or args.text)
    data = None
    is_compressed_warning = False

    with Archive(filepath) as arc:
        try:
            if isinstance(key, int):
                data = bytes(arc.read_hash(key))
            else:
                data = bytes(arc.read(key))
        except BarrelError as e:
            err_code = Archive.get_error_code(e)
            if err_code in (Error.REQUIRES_DECOMPRESSION, Error.NO_DECOMPRESSOR):
                is_compressed_warning = True
                data = bytes(arc.read_copy(key))
            else:
                raise e

    if is_compressed_warning:
        error_console.print("[yellow]⚠ Warning:[/yellow] Entry is compressed or lacks decompressor. Outputting raw blob.")

    if args.out:
        with open(args.out, "wb") as f:
            f.write(data)
        console.print(f"[green]✔[/green] Wrote payload ([bold]{format_bytes(len(data))}[/bold]) to '[bold cyan]{args.out}[/bold cyan]'")
    else:
        hex_dump(data)

def cmd_write(args: argparse.Namespace) -> None:
    filepath = get_archive_path(args)
    key = parse_key(args.name or args.text_key)

    if args.write_file:
        with open(args.write_file, "rb") as f:
            payload = f.read()
    elif args.text is not None:
        payload = args.text.encode("utf-8")
    elif args.bytes is not None:
        payload = parse_bytes_input(args.bytes)
    else:
        raise ValueError("Must specify source content via -f/--data-file, -t/--text, or -b/--bytes")

    with Archive(filepath) as arc:
        arc.write(key, payload, compress=False)

    console.print(f"[green]✔[/green] Wrote [bold]{format_bytes(len(payload))}[/bold] to entry '[bold yellow]{key}[/bold yellow]'")

def cmd_resize(args: argparse.Namespace) -> None:
    filepath = get_archive_path(args)
    Archive.resize_offline(filepath, args.new_size)
    console.print(f"[green]✔[/green] Resized Barrel archive max capacity to [bold]{format_bytes(args.new_size)}[/bold]")

def cmd_pack(args: argparse.Namespace) -> None:
    filepath = get_archive_path(args)
    size_before = os.path.getsize(filepath)

    with Archive(filepath) as arc:
        arc.pack()

    size_after = os.path.getsize(filepath)
    reclaimed = size_before - size_after

    summary = (
        f"Original Size: [dim]{format_bytes(size_before)}[/dim]\n"
        f"Packed Size:   [bold cyan]{format_bytes(size_after)}[/bold cyan]\n"
        f"Reclaimed:     [bold green]{format_bytes(reclaimed)}[/bold green]"
    )
    console.print(Panel(summary, title="[bold green]✔ Archive Packed Successfully[/bold green]", expand=False))

def cmd_info(args: argparse.Namespace) -> None:
    filepath = get_archive_path(args)
    hdr, active_slots = read_index_slots(filepath)
    sig = hdr.signature.decode("ascii", errors="replace")

    flag_names = []
    if hdr.flags & HeaderFlags.PACKED:
        flag_names.append("PACKED")
    if not flag_names:
        flag_names.append("NORMAL")

    flags_display = f"{hdr.flags:#010x} ({' | '.join(flag_names)})"

    table = Table(box=box.ROUNDED, show_header=False, border_style="bright_blue")
    table.add_column("Property", style="bold cyan")
    table.add_column("Value", style="bold white")

    table.add_row("Filepath", filepath)
    table.add_row("Signature", f"[magenta]{sig}[/magenta]")
    table.add_row("Version", f"{hdr.version:#06x}")
    table.add_row("Flags", flags_display)
    table.add_row("Active Files Count", str(hdr.file_count))
    table.add_row("Virtual Capacity", f"{hdr.virtual_capacity} B ({format_bytes(hdr.virtual_capacity)})")
    table.add_row("High Water Mark", f"{hdr.high_water_mark} B ({format_bytes(hdr.high_water_mark)})")
    table.add_row("Index Offset", f"{hdr.index_offset:#010x}")
    table.add_row("Index Capacity (Slots)", str(hdr.index_capacity))
    table.add_row("Packed Virtual Capacity", str(hdr.packed_virtual_capacity))
    table.add_row("Hints", f"{hdr.hints:#010x}")
    table.add_row("Active Index Slots Found", f"[green]{len(active_slots)}[/green]")
    table.add_row("Variant", f"{hdr.variant:#010x}")

    console.print(Panel(table, title="[bold yellow]Barrel Disk Header Info[/bold yellow]", expand=False))

def cmd_list(args: argparse.Namespace) -> None:
    filepath = get_archive_path(args)
    hdr, entries = read_index_slots(filepath)

    if args.plain_text:
        entries = [(idx, meta) for idx, meta in entries if not (meta.flags & EntryFlags.COMPRESSED)]

    if args.only_hash:
        for idx, _ in entries:
            console.print(f"0x{idx:016x}")
        return

    table = Table(
        title=f"Entries in '[bold cyan]{filepath}[/bold cyan]' ({len(entries)} total)",
        box=box.SIMPLE_HEAVY,
        header_style="bold underline magenta",
    )
    table.add_column("Slot / Hash", style="yellow")
    table.add_column("Offset", justify="right", style="dim")
    table.add_column("Size", justify="right", style="cyan")
    table.add_column("Comp. Size", justify="right", style="blue")
    table.add_column("Allocated", justify="right", style="dim")
    table.add_column("Flags", style="bold green")

    for idx, meta in entries:
        flags_str = []
        if meta.flags & EntryFlags.ACTIVE:
            flags_str.append("[green]ACTIVE[/green]")
        if meta.flags & EntryFlags.COMPRESSED:
            flags_str.append("[magenta]COMPRESSED[/magenta]")

        flag_repr = "|".join(flags_str) if flags_str else f"[dim]{meta.flags:#x}[/dim]"

        table.add_row(
            f"0x{idx:016x}",
            str(meta.offset),
            format_bytes(meta.size),
            format_bytes(meta.compressed_size),
            format_bytes(meta.allocated_size),
            flag_repr,
        )

    console.print(table)

def main():
    parser = argparse.ArgumentParser(description="CLI tool for managing Barrel archive files.")
    subparsers = parser.add_subparsers(dest="command", required=True)

    # create
    p_create = subparsers.add_parser("create", help="Create a new empty Barrel archive")
    add_archive_arg(p_create, flag_aliases=("-f", "--file"))
    p_create.add_argument("-i", "--hints", type=int, default=0, help="Hints mask (default: 0)")
    p_create.add_argument("-c", "--init-cap", type=parse_size, default=256, help="Initial index capacity slots (default: 256)")
    p_create.add_argument("-s", "--max-cap", type=parse_size, default=1 << 30, help="Max virtual capacity (e.g., 256M, 1G, 512MiB) (default: 1 GiB)")
    p_create.set_defaults(func=cmd_create)

    # read
    p_read = subparsers.add_parser("read", help="Read entry payload from archive")
    add_archive_arg(p_read)
    key_group = p_read.add_mutually_exclusive_group(required=True)
    key_group.add_argument("-n", "--name", help="Key string name or uint64 hash")
    key_group.add_argument("-t", "--text", help="Key name (alias for -n)")
    p_read.add_argument("-o", "--out", help="Save output to file path")
    p_read.set_defaults(func=cmd_read)

    # write
    p_write = subparsers.add_parser("write", help="Write data entry to archive")
    add_archive_arg(p_write)
    write_key_group = p_write.add_mutually_exclusive_group(required=True)
    write_key_group.add_argument("-n", "--name", help="Target key name or numeric hash")
    write_key_group.add_argument("-k", "--key", dest="text_key", help="Target key name (alias for -n)")

    data_group = p_write.add_mutually_exclusive_group(required=True)
    data_group.add_argument("-f", "--data-file", dest="write_file", help="Write data from input file path")
    data_group.add_argument("-t", "--text", help="Write UTF-8 text string")
    data_group.add_argument("-b", "--bytes", help="Write raw bytes ('0xAA0xBB', '0xAA,0xBB', '\\xAA\\xBB')")
    p_write.set_defaults(func=cmd_write)

    # resize
    p_resize = subparsers.add_parser("resize", help="Resize archive max capacity offline")
    add_archive_arg(p_resize)
    p_resize.add_argument("-s", "--new-size", type=parse_size, required=True, help="New max virtual capacity (e.g., 512M, 2G)")
    p_resize.set_defaults(func=cmd_resize)

    # pack
    p_pack = subparsers.add_parser("pack", help="Pack archive to reclaim fragmented space and truncate file")
    add_archive_arg(p_pack)
    p_pack.set_defaults(func=cmd_pack)

    # info
    p_info = subparsers.add_parser("info", help="Dump archive header and structural information")
    add_archive_arg(p_info)
    p_info.set_defaults(func=cmd_info)

    # list
    p_list = subparsers.add_parser("list", help="List all index entries in archive")
    add_archive_arg(p_list)
    p_list.add_argument("-n", "--only-hash", action="store_true", help="Only output hashes")
    p_list.add_argument("-p", "--plain-text", action="store_true", help="Only list uncompressed entries")
    p_list.set_defaults(func=cmd_list)

    args = parser.parse_args()
    try:
        args.func(args)
    except Exception as err:
        error_console.print(Panel(f"[bold red]Error:[/bold red] {err}", border_style="red"))
        sys.exit(1)
