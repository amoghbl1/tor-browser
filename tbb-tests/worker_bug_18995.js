// Attempt to open a cache
caches.open("test2").then(function (value) {
  // This is not supposed to happen.
  self.postMessage(value.toString());
}, function (reason) {
  // We are supposed to fail.
  self.postMessage(reason.toString());
});
