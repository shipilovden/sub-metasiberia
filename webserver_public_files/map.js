function getQueryParam(name) {
    try {
        const params = new URLSearchParams(window.location.search);
        return params.get(name) || '';
    } catch (e) {
        return '';
    }
}

const worldParam = getQueryParam('world').trim();
const isRootWorld = (worldParam === '' || worldParam.toLowerCase() === 'root');

let mymap = L.map('mapid', { crs: L.CRS.Simple }).setView([0.0, 0.0], 4);

// Allow "super zoom" (overzoom) without generating extra tiles on the server.
const minNativeZoom = 0;
const maxNativeZoom = 6;
const minZoom = 0;
const maxZoom = 10;

if (isRootWorld) {
    L.tileLayer('/tile?x={x}&y={y}&z={z}', {
        zoomOffset: 0,
        minZoom: minZoom,
        maxZoom: maxZoom,
        minNativeZoom: minNativeZoom,
        maxNativeZoom: maxNativeZoom,
        noWrap: true,
    }).addTo(mymap);
} else {
    // For non-root worlds we currently show a lightweight local grid background (no server tiles),
    // while still rendering parcel outlines for the selected world.
    L.gridLayer({
        minZoom: minZoom,
        maxZoom: maxZoom,
        noWrap: true,
        createTile: function (coords) {
            const tile = document.createElement('canvas');
            tile.width = 256;
            tile.height = 256;

            const ctx = tile.getContext('2d');
            ctx.fillStyle = '#f7f9fb';
            ctx.fillRect(0, 0, 256, 256);

            ctx.strokeStyle = '#e3e8ef';
            ctx.lineWidth = 1;
            for (let x = 0; x <= 256; x += 32) {
                ctx.beginPath();
                ctx.moveTo(x + 0.5, 0);
                ctx.lineTo(x + 0.5, 256);
                ctx.stroke();
            }
            for (let y = 0; y <= 256; y += 32) {
                ctx.beginPath();
                ctx.moveTo(0, y + 0.5);
                ctx.lineTo(256, y + 0.5);
                ctx.stroke();
            }
            return tile;
        },
    }).addTo(mymap);
}


var info = L.control();

info.onAdd = function (map) {
    this._div = L.DomUtil.create('div', 'map-info'); // create a div with a class "info"
    this.update();
    return this._div;
};


const MRADMIN_COL = '#3388ff'; // blue
const OTHER_OWNED_COL = '#0f9caa'; // cyan
const FOR_AUCTION_COL = '#b40d96'; // purpleish
const HIGHLIGHTED_COL = '#ef9518';


// method that we will use to update the control based on feature properties passed
info.update = function (props) {
    this._div.innerHTML =
        "<span class=\"map-col-mradmin\">&#9632;</span> Owned by Substrata<br/>" +
        "<span class=\"map-col-other\">&#9632;</span> Owned by other<br/>" +
        "<span class=\"map-col-for-auction\">&#9632;</span> Currently at auction";

};

info.addTo(mymap);



/*mymap.on('click', function (e) {
    console.log("lat: " + e.latlng.lat);
    console.log("lng: " + e.latlng.lng);
});*/


/*L.GridLayer.GridDebug = L.GridLayer.extend({
    createTile: function (coords) {
        const tile = document.createElement('div');
        tile.style.outline = '1px solid green';
        tile.style.fontWeight = 'bold';
        tile.style.fontSize = '14pt';
        tile.innerHTML = [coords.x, coords.y, coords.z].join('/');
        return tile;
    },
});

L.gridLayer.gridDebug = function (opts) {
    return new L.GridLayer.GridDebug(opts);
};

mymap.addLayer(L.gridLayer.gridDebug());*/


var elems = document.getElementById("poly_coords").textContent.split(',');
var poly_coords = []
for (let i = 0; i < elems.length; i += 2) {
    poly_coords.push([elems[i], elems[i + 1]]);
}

var poly_parcel_ids = document.getElementById("poly_parcel_ids").textContent.split(',').map(s => parseInt(s, /*radix=*/10));

var poly_parcel_names = document.getElementById("poly_parcel_names").textContent.split(';');

var poly_parcel_state = document.getElementById("poly_parcel_state").textContent.split(',').map(s => parseInt(s, /*radix=*/10));

var rect_bound_coords = document.getElementById("rect_bound_coords").textContent.split(',').map(s => parseFloat(s));

var rect_parcel_ids = document.getElementById("rect_parcel_ids").textContent.split(',').map(s => parseInt(s, /*radix=*/10));

var rect_parcel_names = document.getElementById("rect_parcel_names").textContent.split(';');

var rect_parcel_state = document.getElementById("rect_parcel_state").textContent.split(',').map(s => parseInt(s, /*radix=*/10));

var highlight_parcel_id = parseInt(document.getElementById("highlight_parcel_id").textContent, /*radix=*/10);

function escapeHtml(unsafe) {
    return unsafe
        .replace(/&/g, "&amp;")
        .replace(/</g, "&lt;")
        .replace(/>/g, "&gt;")
        .replace(/"/g, "&quot;")
        .replace(/'/g, "&#039;");
}


for (let i = 0; i < poly_parcel_ids.length; i++) {

    let is_highlighted_parcel = poly_parcel_ids[i] == highlight_parcel_id;
    let state = poly_parcel_state[i];

    // 0 = owned by MrAdmin and not on auction, 1 = owned by MrAdmin and for auction, 2 = owned by someone else
    let colour = null;
    if (state == 0)
        colour = MRADMIN_COL
    else if (state == 1)
        colour = FOR_AUCTION_COL
    else
        colour = OTHER_OWNED_COL

    if (is_highlighted_parcel)
        colour = HIGHLIGHTED_COL

    let vert_i = i * 4;
    let polygon = L.polygon([
        poly_coords[vert_i + 0],
        poly_coords[vert_i + 1],
        poly_coords[vert_i + 2],
        poly_coords[vert_i + 3],
    ], {
        weight: 2, // line width in pixels
        color: colour,
        //fillColor: '#777',
        fillOpacity: (is_highlighted_parcel ? 0.2 : 0.05)
    }).addTo(mymap);

    let title = (poly_parcel_names[i].length == 0) ? ("Parcel " + poly_parcel_ids[i].toString()) : escapeHtml(poly_parcel_names[i]);

    let popup = polygon.bindPopup("<a href=\"/parcel/" + poly_parcel_ids[i].toString() + "\">" + title + "</a>");

    if (is_highlighted_parcel)
        popup.openPopup();
}


for (let i = 0; i < rect_parcel_ids.length; i++) {

    let is_highlighted_parcel = rect_parcel_ids[i] == highlight_parcel_id;
    let state = rect_parcel_state[i];

    let margin = 0.01

    // 0 = owned by MrAdmin and not on auction, 1 = owned by MrAdmin and for auction, 2 = owned by someone else
    let colour = null;
    if (state == 0)
        colour = MRADMIN_COL
    else if (state == 1)
        colour = FOR_AUCTION_COL
    else
        colour = OTHER_OWNED_COL

    if (is_highlighted_parcel)
        colour = HIGHLIGHTED_COL;

    let coord_i = i * 4;
    let polygon = L.rectangle(
        [[rect_bound_coords[coord_i + 0] + margin, rect_bound_coords[coord_i + 1] + margin], [rect_bound_coords[coord_i + 2] - margin, rect_bound_coords[coord_i + 3] - margin]],
        {
        weight: 2, // line width in pixels
        color: colour,
        //fillColor: '#777',
        fillOpacity: (is_highlighted_parcel ? 0.2 : 0.05)
    }).addTo(mymap);

    let title = (rect_parcel_names[i].length == 0) ? ("Parcel " + rect_parcel_ids[i].toString()) : escapeHtml(rect_parcel_names[i]);

    let popup = polygon.bindPopup("<a href=\"/parcel/" + rect_parcel_ids[i].toString() + "\">" + title + "</a>");

    if (is_highlighted_parcel)
        popup.openPopup();
}
