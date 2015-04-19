// Wait for a blob URL to be posted to this worker.
// Obtain the blob, and read the string contained in it.
// Post back the string.

var postStringInBlob = function (blobObject) {
  var fileReader = new FileReaderSync(),
      result = fileReader.readAsText(blobObject);
  postMessage(result);
};

self.addEventListener("message", function (e) {
  var blobURL = e.data,
      xhr = new XMLHttpRequest();
  try {
    xhr.open("GET", blobURL, true);
    xhr.onload = function () {
      postStringInBlob(xhr.response);
    };
    xhr.responseType = "blob";
    xhr.send();
  } catch (e) {
    postMessage(e.message);
  }
}, false);
