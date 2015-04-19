// __listen(target, eventType, timeoutMs, useCapture)__.
// Calls addEventListener on target, with the given eventType.
// Returns a Promise that resolves to an Event object, if the event fires.
// If a timeout occurs, then Promise is rejected with a "Timed out" error.
// For use with SpawnTask.js.
let listen = function (target, eventType, timeoutMs, useCapture) {
  return new Promise(function (resolve, reject) {
    let listenFunction = function (event) {
      target.removeEventListener(eventType, listenFunction, useCapture);
      resolve(event);
    };
    target.addEventListener(eventType, listenFunction, useCapture);
    if (timeoutMs) {
      setTimeout(() => reject(new Error("Timed out")), timeoutMs);
    }
  });
};

// __receiveMessage(source)__.
// Returns an event object for the next message received from source.
// A SpawnTask.js coroutine.
let receiveMessage = function* (source) {
  let event;
  do {
    event = yield listen(self, "message", null, false);
  } while (event.source !== source);
  return event.data;
};

// __sendMessage(destination, message)__.
// Sends a message to destination.
let sendMessage = function (destination, message) {
  destination.postMessage(message, "*");
};

// __appendLine(id, lineString)__.
// Add a line of text to the innerHTML of element with id.
let appendLine = function (id, lineString) {
  document.getElementById(id).innerHTML += lineString + "\n";
};

// __xhr(method, url, responseType__.
// A simple async XMLHttpRequest call.
// Returns a promise with the response.
let xhr = function (method, url, responseType) {
  return new Promise(function (resolve, reject) {
    let xhr = new XMLHttpRequest();
    xhr.open(method, url, true);
    xhr.onload = function () {
      resolve(xhr.response);
    };
    xhr.responseType = responseType;
    xhr.send();
  });
};

// __blobURLtoBlob(blobURL)__.
// Asynchronously retrieves a blob object
// from a blob URL. Returns a promise.
let blobURLtoBlob = function (blobURL) {
  return xhr("GET", blobURL, "blob");
};

// __blobToString(blob)__.
// Asynchronously reads the contents
// of a blob object into a string. Returns a promise.
let blobToString = function (blob) {
  return new Promise(function (resolve, reject) {
    let fileReader = new FileReader();
    fileReader.onload = function () {
      resolve(fileReader.result);
    };
    fileReader.readAsText(blob);
  });
};

// __blobURLtoString(blobURL)__.
// Asynchronous coroutine that takes a blobURL
// and returns the contents in a string.
let blobURLtoString = function* (blobURL) {
  let blob = yield blobURLtoBlob(blobURL);
  return yield blobToString(blob);
};

// __stringToBlobURL(s)__.
// Converts string s into a blob, and returns
// a blob URL.
let stringToBlobURL = function (s) {
  let blob = new Blob([s]);
  return URL.createObjectURL(blob);
};

// __workerIO(scriptFile, inputString)__.
// Sends inputString for the worker, and waits
// for the worker to return an outputString.
// SpawnTask.js coroutine.
let workerIO = function* (scriptFile, inputString) {
  let worker = new Worker(scriptFile);
  worker.postMessage(inputString);
  let result = yield listen(worker, "message", 5000, false);
  worker.terminate();
  return result.data;
};
