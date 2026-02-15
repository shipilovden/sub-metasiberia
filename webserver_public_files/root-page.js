
// Code for fading in and out header images.
// Adapted from https://stackoverflow.com/a/25348073
//
// NOTE: Metasiberia may use a simplified root page without a hero slideshow.
// In that case, just no-op.

var pathsEl = document.getElementById("top-image-paths");
var imgEl = document.getElementById("top-image");
if (!pathsEl || !imgEl) {
  // Root page doesn't have slideshow elements.
  // Keep script harmless to avoid breaking the page with JS errors.
  // eslint-disable-next-line no-undef
  // (No logging: avoid console noise for users.)
  // Return early.
  // eslint-disable-next-line no-useless-return
  return;
}

// Read image paths from 'top-image-paths' element in root_page.htmlfrag
var imgArray = pathsEl.textContent.split(',').map(s => s.trim()).filter(s => s.length > 0);

var curIndex = 0;
var imgDuration = 7000;

function transitionImage() {
    curIndex++;
    if (curIndex == imgArray.length) { curIndex = 0; }

    document.getElementById('top-image').className = "top-image fadeOut"; // Start fading out by adding fadeOut class
    setTimeout(function () { // Once we have faded out, change to next image.
        document.getElementById('top-image').src = imgArray[curIndex]; // Change to next image
        document.getElementById('top-image').className = "top-image"; // Remove fadeOut class
    }, 2000); // NOTE: this timeout value should match the time of the transition setting in #top-image in main.css.
   
    setTimeout(transitionImage, imgDuration);
}

if (imgArray.length > 1)
  setTimeout(transitionImage, imgDuration - 2000);
