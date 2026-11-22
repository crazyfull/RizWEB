# Applying this on top of ArsPand/RizWEB

```sh
git checkout -b netrector-migration
# remove the old libwrench based code
git rm -r src/HTTP src/LinuxService RizWEB.pro main.cpp
git rm -f src/cls*.h src/cls*.cpp 2>/dev/null || true
# copy this archive's contents over the repository (keep resource/, upload/ untouched)
cp -r /path/to/RizWEB/{CMakeLists.txt,README.md,.gitignore,src,tests} .
git add -A && git commit -m "Rewrite on netRector, remove libwrench, fix security issues"
```

Keep `resource/` (brows.css, icons, mime.types.db). `src/LinuxService` is not ported yet.
