-- Reset View: an example tool plugin (MIT licence).
-- Tools run program commands by their id (needs the "commands" permission).

function run(params)
  mxj.command("edit.selectNone")
  mxj.command("view.zoomFull")
  mxj.command("transport.toStart")
  return "The whole file is shown and the cursor is at the start."
end
