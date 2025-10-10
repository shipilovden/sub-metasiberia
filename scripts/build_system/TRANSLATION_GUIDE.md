# 🌍 Metasiberia Translation System

## 📋 Translation System Overview
Metasiberia supports multilingual interface through Qt Translation System. Users can switch between languages in real-time.

## 📁 Translation Files Structure

```
substrata/
├── resources/
│   └── translations/
│       ├── metasiberia_ru.ts     # Source translation file (Russian)
│       └── metasiberia_ru.qm     # Compiled translation file
└── gui_client/
    ├── MainWindow.h              # Translation declarations
    └── MainWindow.cpp            # Language switching logic
```

## 🔧 How to Add New Language

### **1. Create Translation File (.ts)**

**Copy existing file:**
```bash
# Navigate to translations folder
cd C:\programming\substrata\resources\translations

# Copy Russian file as template
copy metasiberia_ru.ts metasiberia_es.ts  # For Spanish
copy metasiberia_ru.ts metasiberia_de.ts  # For German
copy metasiberia_ru.ts metasiberia_fr.ts  # For French
```

### **2. Edit Translation File**

**Open .ts file in text editor and find sections:**

```xml
<context>
    <name>MainWindow</name>
    <message>
        <source>Language</source>
        <translation>Язык</translation>
    </message>
    <message>
        <source>English</source>
        <translation>Английский</translation>
    </message>
    <message>
        <source>Русский</source>
        <translation>Русский</translation>
    </message>
</context>
```

**Replace translations with target language:**
```xml
<!-- For Spanish (es) -->
<translation>Idioma</translation>        <!-- Language -->
<translation>Inglés</translation>        <!-- English -->
<translation>Español</translation>       <!-- Spanish -->
```

### **3. Add Language to Code**

**In `MainWindow.cpp` find menu creation section:**

```cpp
// Add new language to menu
action_lang_es = language_menu->addAction(tr("Español"));
action_lang_es->setCheckable(true);
action_lang_es->setData(QString("es"));
language_action_group->addAction(action_lang_es);
```

**In `MainWindow.h` add declaration:**
```cpp
QAction* action_lang_es = nullptr;  // For Spanish
```

### **4. Add Loading Logic**

**In `MainWindow.cpp` find language switching function:**

```cpp
connect(language_action_group, &QActionGroup::triggered, this, [this](QAction* a){
    const QString lang = a->data().toString();
    qApp->removeTranslator(&app_translator);
    bool installed = false;
    
    if(lang == "ru") {
        // Russian
        QString qm_path = QString::fromStdString(base_dir_path) + "/data/resources/translations/metasiberia_ru.qm";
        if(QFile::exists(qm_path)) {
            installed = app_translator.load(qm_path);
            if(installed) qApp->installTranslator(&app_translator);
        }
    }
    else if(lang == "es") {  // ADD THIS
        // Spanish
        QString qm_path = QString::fromStdString(base_dir_path) + "/data/resources/translations/metasiberia_es.qm";
        if(QFile::exists(qm_path)) {
            installed = app_translator.load(qm_path);
            if(installed) qApp->installTranslator(&app_translator);
        }
    }
    // For English, we don't install translator
    
    // Save language preference
    if(settings) settings->setValue("mainwindow/language", lang);
    
    // Update interface
    ui->retranslateUi(this);
});
```

### **5. Compile Translation**

**Add to `build_metasiberia.bat` processing for new language:**

```batch
REM Add after existing metasiberia_ru.qm processing
if defined LRELEASE (
    echo Generating metasiberia_es.qm...
    "%LRELEASE%" -silent "C:\programming\substrata\resources\translations\metasiberia_es.ts" -qm "%EXE_DIR%\translations\metasiberia_es.qm"
)
```

## 🛠️ Translation Tools

### **Qt Linguist (recommended):**
```bash
# Launch Qt Linguist
C:\programming\Qt\5.15.16-vs2022-64\bin\linguist.exe
```

**How to use:**
1. Open `.ts` file in Qt Linguist
2. Select string to translate
3. Enter translation in bottom panel
4. Save file

### **Manual editing:**
- Open `.ts` file in any text editor
- Find `<translation>` tags and replace content
- Save file

## 🔄 Translation Update Process

### **1. After modifying .ts file:**
```bash
# Rebuild project
C:\programming\Metasiberia_Build_System\build_metasiberia.bat
```

### **2. Test result:**
```bash
# Launch client
C:\programming\Metasiberia_Build_System\run_gui_client.bat
```

## 📝 Translation Examples

### **Russian (ru):**
```xml
<source>Language</source>
<translation>Язык</translation>

<source>English</source>
<translation>Английский</translation>

<source>Русский</source>
<translation>Русский</translation>
```

### **Spanish (es):**
```xml
<source>Language</source>
<translation>Idioma</translation>

<source>English</source>
<translation>Inglés</translation>

<source>Русский</source>
<translation>Español</translation>
```

### **German (de):**
```xml
<source>Language</source>
<translation>Sprache</translation>

<source>English</source>
<translation>Englisch</translation>

<source>Русский</source>
<translation>Deutsch</translation>
```

## ⚠️ Important Notes

1. **Always test** translations after changes
2. **Keep backup copies** of .ts files
3. **Use UTF-8** encoding for translation files
4. **Check translation length** - they might not fit in interface

## 🎯 Available Translations

**Current languages:**
- ✅ **English** (default)
- ✅ **Russian** (metasiberia_ru.qm)

**Can be added:**
- 🇪🇸 **Español** (Spanish)
- 🇩🇪 **Deutsch** (German)  
- 🇫🇷 **Français** (French)
- 🇨🇳 **中文** (Chinese)
- 🇯🇵 **日本語** (Japanese)

---

**Metasiberia Translation System is ready!** 🎉
