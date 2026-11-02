HZ = os.sysconf("SC_CLK_TCK")
def threads(pid):
    res = {}
    for t in Path(f"/proc/{pid}/task").iterdir():
        try:
            st = (t / "stat").read_text()
            name = st[st.index("(")+1:st.rindex(")")]
            f = st[st.rindex(")")+2:].split()
            res[int(t.name)] = (name, int(f[11]) + int(f[12]))
        except Exception: pass
    return res
def diff(a, b, dt):
    by = {}
    for tid, (name, ticks) in b.items():
        by[name] = by.get(name, 0) + 100 * (ticks - a.get(tid, (name, 0))[1]) / HZ / dt
    return by
