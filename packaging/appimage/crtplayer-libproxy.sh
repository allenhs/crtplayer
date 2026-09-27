# AppRun hook: use the bundled libproxy stand-in only when the system has no libproxy.so.1
# (minimal installs). With a system libproxy, it is used as before.
_crt_has_libproxy=""
if { ldconfig -p 2>/dev/null || /sbin/ldconfig -p 2>/dev/null; } | grep -q 'libproxy\.so\.1 '; then _crt_has_libproxy=1; fi
for _d in /usr/lib64 /usr/lib /usr/lib/x86_64-linux-gnu /lib64 /lib/x86_64-linux-gnu; do
    [ -e "$_d/libproxy.so.1" ] && _crt_has_libproxy=1
done
if [ -z "$_crt_has_libproxy" ]; then
    export LD_LIBRARY_PATH="$APPDIR/usr/lib/fallback${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}"
fi
unset _crt_has_libproxy _d
