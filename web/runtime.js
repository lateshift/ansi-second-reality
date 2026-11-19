// callMain returns as soon as Asyncify first unwinds. Wait for the original
// call to finish so the host cannot replace a still-running WASM instance.
Module["runPlayback"] = async args => {
  const result = callMain(args);
  return Asyncify.currData ? Asyncify.whenDone() : result;
};
