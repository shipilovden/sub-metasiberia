/*
 * Metasiberia Gaussian splat viewer bootstrap.
 *
 * The renderer can consume PLY and SOG directly. Other supported Gaussian
 * formats are converted in the browser to a temporary compressed PLY object
 * URL by the self-hosted @playcanvas/splat-transform bundle.
 */

const DIRECT_VIEWER_FILENAMES = new Set(['meta.json', 'lod-meta.json']);
const DIRECT_VIEWER_EXTENSIONS = new Set(['ply', 'sog']);
const CONVERTIBLE_EXTENSIONS = new Set([
    'splat',
    'ksplat',
    'spz',
    'lcc',
    'lcc2'
]);

const DEFAULT_SETTINGS = Object.freeze({
    version: 2,
    tonemapping: 'none',
    highPrecisionRendering: false,
    background: {
        color: [0.025, 0.035, 0.055]
    },
    postEffectSettings: {
        sharpness: { enabled: false, amount: 0 },
        bloom: { enabled: false, intensity: 1, blurLevel: 2 },
        grading: {
            enabled: false,
            brightness: 0,
            contrast: 1,
            saturation: 1,
            tint: [1, 1, 1]
        },
        vignette: {
            enabled: false,
            intensity: 0.5,
            inner: 0.3,
            outer: 0.75,
            curvature: 1
        },
        fringing: { enabled: false, intensity: 0.5 }
    },
    animTracks: [],
    cameras: [],
    annotations: [],
    startMode: 'default'
});

let converterScriptPromise;

const createImage = (url) => {
    if (!url) {
        return null;
    }

    const image = new Image();
    image.crossOrigin = 'anonymous';
    image.src = url;
    return image;
};

const fetchChecked = async (url) => {
    const response = await fetch(url);
    if (!response.ok) {
        throw new Error(`HTTP ${response.status} while loading ${url}`);
    }
    return response;
};

const cloneDefaultSettings = () => JSON.parse(JSON.stringify(DEFAULT_SETTINGS));

export const getGaussianSourceInfo = (contentUrl, baseUrl = window.location.href) => {
    const absoluteUrl = new URL(contentUrl, baseUrl);
    const pathname = decodeURIComponent(absoluteUrl.pathname);
    const filename = pathname.substring(pathname.lastIndexOf('/') + 1);
    const lowerFilename = filename.toLowerCase();
    const extension = lowerFilename.endsWith('.compressed.ply')
        ? 'ply'
        : lowerFilename.substring(lowerFilename.lastIndexOf('.') + 1);

    return {
        absoluteUrl: absoluteUrl.href,
        filename,
        extension,
        direct: DIRECT_VIEWER_FILENAMES.has(lowerFilename) ||
            DIRECT_VIEWER_EXTENSIONS.has(extension),
        convertible: CONVERTIBLE_EXTENSIONS.has(extension)
    };
};

export const loadGaussianConverter = (
    scriptUrl = '/files/gaussian_splat_converter.js',
    documentRef = document
) => {
    const existingConverter = window.MetasiberiaGaussianSplatConverter ??
        window.MetasiberiaSplatConverter;
    if (existingConverter) {
        return Promise.resolve(existingConverter);
    }

    if (!converterScriptPromise) {
        converterScriptPromise = new Promise((resolve, reject) => {
            const script = documentRef.createElement('script');
            script.src = scriptUrl;
            script.async = true;
            script.dataset.metasiberiaGaussianConverter = 'true';
            script.addEventListener('load', () => {
                const converter = window.MetasiberiaGaussianSplatConverter ??
                    window.MetasiberiaSplatConverter;
                if (converter) {
                    resolve(converter);
                } else {
                    reject(new Error('Gaussian converter loaded without exposing its API.'));
                }
            }, { once: true });
            script.addEventListener('error', () => {
                reject(new Error(`Failed to load Gaussian converter: ${scriptUrl}`));
            }, { once: true });
            documentRef.head.appendChild(script);
        });
    }

    return converterScriptPromise;
};

const loadSettings = async (settingsParam, pageUrl) => {
    if (!settingsParam) {
        return cloneDefaultSettings();
    }

    const settingsUrl = new URL(settingsParam, pageUrl).href;
    const response = await fetchChecked(settingsUrl);
    return response.json();
};

const getBudget = (searchParams) => {
    if (!searchParams.has('budget')) {
        return undefined;
    }

    const value = Number(searchParams.get('budget'));
    return Number.isFinite(value) && value > 0 ? value : undefined;
};

const conversionStatus = (progress) => {
    switch (progress.stage) {
    case 'read':
        return { text: 'Чтение Gaussian Splat…', percent: 20 };
    case 'convert':
        return { text: 'Преобразование в compressed PLY…', percent: 55 };
    case 'ready':
        return { text: 'Подготовка 3D-сцены…', percent: 90 };
    default:
        return { text: 'Подготовка Gaussian Splat…', percent: 10 };
    }
};

export const prepareGaussianViewer = async ({
    pageUrl = new URL(window.location.href),
    updateStatus = () => {},
    converterLoader = loadGaussianConverter
} = {}) => {
    const contentParam = pageUrl.searchParams.get('content');
    if (!contentParam) {
        throw new Error(
            'Не указан Gaussian Splat. Откройте страницу с параметром ?content=<URL-файла>.'
        );
    }

    const source = getGaussianSourceInfo(contentParam, pageUrl);
    if (!source.direct && !source.convertible) {
        throw new Error(
            `Формат “.${source.extension || '?'}” не поддерживается. ` +
            'Используйте PLY, compressed PLY, SOG, SPLAT, KSPLAT, SPZ, LCC или LCC2.'
        );
    }

    const settingsPromise = loadSettings(pageUrl.searchParams.get('settings'), pageUrl);
    let runtimeUrl = source.absoluteUrl;
    let viewerContentUrl = source.absoluteUrl;
    let release = () => {};

    if (!source.direct) {
        updateStatus({
            text: 'Загрузка конвертера Gaussian Splat…',
            percent: 5
        });
        const converter = await converterLoader();
        const converted = await converter.convertToViewerUrl(
            source.absoluteUrl,
            (progress) => updateStatus(conversionStatus(progress))
        );

        runtimeUrl = converted.url;
        // The PlayCanvas asset loader selects its parser from this semantic
        // filename. Blob URLs have no extension, while their Response contains
        // the generated compressed PLY payload.
        viewerContentUrl = new URL(
            converted.filename || 'metasiberia-runtime.compressed.ply',
            pageUrl
        ).href;
        release = converted.revoke;
    } else {
        updateStatus({
            text: 'Загрузка Gaussian Splat…',
            percent: 5
        });
    }

    const posterParam = pageUrl.searchParams.get('poster');
    const skyboxParam = pageUrl.searchParams.get('skybox');
    const collisionParam = pageUrl.searchParams.get('collision') ??
        pageUrl.searchParams.get('voxel');
    const renderer = pageUrl.searchParams.has('webgl') ? 'webgl' : 'webgpu';

    const config = {
        poster: createImage(posterParam && new URL(posterParam, pageUrl).href),
        skyboxUrl: skyboxParam ? new URL(skyboxParam, pageUrl).href : null,
        collisionUrl: collisionParam ? new URL(collisionParam, pageUrl).href : null,
        contentUrl: viewerContentUrl,
        contents: fetchChecked(runtimeUrl),
        noui: pageUrl.searchParams.has('noui'),
        noanim: pageUrl.searchParams.has('noanim'),
        nofx: pageUrl.searchParams.has('nofx'),
        hpr: pageUrl.searchParams.has('hpr')
            ? ['', '1', 'true', 'enable'].includes(pageUrl.searchParams.get('hpr'))
            : undefined,
        ministats: pageUrl.searchParams.has('ministats'),
        colorize: pageUrl.searchParams.has('colorize'),
        renderer,
        aa: pageUrl.searchParams.has('aa'),
        budget: getBudget(pageUrl.searchParams),
        heatmap: pageUrl.searchParams.has('heatmap'),
        fullload: pageUrl.searchParams.has('fullload'),
        debug: pageUrl.searchParams.has('debug'),
        lang: pageUrl.searchParams.get('lang')
    };

    try {
        return {
            config,
            settings: settingsPromise,
            release,
            source
        };
    } catch (error) {
        release();
        throw error;
    }
};

export const updateGaussianViewerStatus = ({ text, percent, error = false }) => {
    const wrap = document.getElementById('loadingWrap');
    const textElement = document.getElementById('loadingText');
    const bar = document.getElementById('loadingBar');

    if (!wrap || !textElement || !bar) {
        return;
    }

    wrap.classList.toggle('metasiberia-viewer-error', error);
    textElement.textContent = text;

    if (!error) {
        const safePercent = Math.max(0, Math.min(100, Number(percent) || 0));
        bar.style.backgroundImage =
            `linear-gradient(90deg, #32d6c5 0%, #32d6c5 ${safePercent}%, ` +
            `rgba(255, 255, 255, 0.28) ${safePercent}%, rgba(255, 255, 255, 0.28) 100%)`;
    }
};

export const showGaussianViewerError = (error) => {
    console.error(error);
    updateGaussianViewerStatus({
        text: `Gaussian Splat не загружен.\n${error?.message || String(error)}`,
        percent: 0,
        error: true
    });
};
