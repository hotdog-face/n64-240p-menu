#!/bin/bash
# One-command backup of everything irreplaceable on the N64 SD card.
# Usage: bash backup_card.sh   (card must be mounted at /Volumes/E64)
# Backs up: game saves, favorites/history/config, gameconfigs — into timestamped
# folders under ~/n64/save_backups (keeps every run; they're tiny).
set -euo pipefail

CARD="/Volumes/E64"
DEST="$HOME/n64/save_backups/$(date +%Y-%m-%d_%H%M)"

if [ ! -d "$CARD" ]; then
  echo "Card not mounted at $CARD — insert the SD card first."
  exit 1
fi

mkdir -p "$DEST"

# Every saves/ folder next to games (Games A-M, Games N-Z, Extras, 00_playing...)
# (find exits nonzero on macOS-protected dirs like .Spotlight-V100 — harmless, ignore)
{ find "$CARD" -maxdepth 2 -type d -name saves 2>/dev/null || true; } | while read -r d; do
  rel="${d#"$CARD"/}"
  mkdir -p "$DEST/$rel"
  rsync -a --exclude='._*' "$d/" "$DEST/$rel/"
done

# Menu state: favorites, history, config, per-game configs
mkdir -p "$DEST/menu"
rsync -a --exclude='._*' --exclude='boxart' --exclude='cache' "$CARD/menu/" "$DEST/menu/"

# EverDrive-native saves (games launched via stock OS historically)
if [ -d "$CARD/ED64/SAVE" ]; then
  mkdir -p "$DEST/ED64_SAVE"
  rsync -a --exclude='._*' "$CARD/ED64/SAVE/" "$DEST/ED64_SAVE/"
fi

echo "Backed up to: $DEST"
du -sh "$DEST"
