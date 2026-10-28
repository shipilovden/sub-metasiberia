#pragma once

#include <QtCore/QByteArray>
#include <QtCore/QDateTime>
#include <QtCore/QString>
#include <QtCore/QStringList>
#include <QtCore/QVector>
#include <QtGui/QIcon>


struct PluginPackageInfo
{
	QString id;
	QString name;
	QString author;
	QString version;
	QString description;
	QString runtime;
	QString entryPoint;
	QString source;
	QString iconPath;
	QIcon icon;
	QStringList permissions;
	QStringList categories;
	QByteArray packageSha256;
	QByteArray installedContentSha256;
	int apiMajor = 0;
	int apiMinor = 0;
	bool apiCompatible = false;
	bool installed = false;
	bool enabled = false;
	bool running = false;
	QString state;
	QString runtimeError;
	QString installPath;
	QDateTime installedAt;
};


struct PluginPackageFile
{
	QString path;
	QByteArray contents;
	QByteArray sha256;
};


// Validates the inert .mspkg ZIP profile and its plugin.json manifest.
// It never loads or executes package code.
class PluginPackageValidator
{
public:
	static constexpr int CurrentApiMajor = 1;
	static constexpr int CurrentApiMinor = 2;
	static constexpr qint64 MaximumPackageBytes = 64LL * 1024 * 1024;
	static constexpr qint64 MaximumExpandedBytes = 64LL * 1024 * 1024;
	static constexpr qint64 MaximumFileBytes = 16LL * 1024 * 1024;
	static constexpr int MaximumFileCount = 512;
	static constexpr int MaximumManifestBytes = 64 * 1024;

	static int compareVersions(const QString& left, const QString& right);
	static bool inspect(const QString& packagePath, PluginPackageInfo& info, QString& error);
	static bool readPackage(const QString& packagePath, PluginPackageInfo& info,
		QVector<PluginPackageFile>& files, QString& error);
	static bool readInstalledDirectory(const QString& directoryPath, PluginPackageInfo& info,
		QString& error, bool loadIcon = true);
	static QString pathValidationError(const QString& path, bool allowDirectory = false);

private:
	static bool parseManifest(const QByteArray& data, const QStringList& packageFiles,
		PluginPackageInfo& info, QString& error);
};


// Local package/install catalog. Store entries are provided separately by a
// provider; the default provider intentionally has no remote entries.
class PluginStoreCatalogProvider
{
public:
	virtual ~PluginStoreCatalogProvider() {}
	virtual bool isAvailable() const = 0;
	virtual QString statusText() const = 0;
	virtual QList<PluginPackageInfo> search(const QString& query, QString& error) const = 0;
	virtual bool canDownload(const PluginPackageInfo&) const { return false; }
	virtual bool downloadPackage(const PluginPackageInfo&, const QString&, QString& error) const;
};


class EmptyPluginStoreCatalogProvider final : public PluginStoreCatalogProvider
{
public:
	bool isAvailable() const override { return false; }
	QString statusText() const override;
	QList<PluginPackageInfo> search(const QString& query, QString& error) const override;
};


class PluginPackageManager
{
public:
	explicit PluginPackageManager(const QString& rootDirectory = QString(),
		PluginStoreCatalogProvider* provider = nullptr, const QString& settingsRoot = QString(), bool loadIcons = true);

	QString rootDirectory() const;
	QString settingsRootDirectory() const;
	QString settingsDirectoryForId(const QString& pluginId) const;
	QList<PluginPackageInfo> installedPlugins() const;
	QList<PluginPackageInfo> storePlugins(const QString& query, QString& error) const;
	QString storeStatusText() const;
	bool canDownloadStorePackage(const PluginPackageInfo& info) const;
	bool downloadStorePackage(const PluginPackageInfo& info, const QString& destinationPath, QString& error) const;

	bool inspectPackage(const QString& packagePath, PluginPackageInfo& info, QString& error) const;
	bool installPackage(const QString& packagePath, PluginPackageInfo& installed, QString& error,
		const QByteArray& expectedPackageSha256 = QByteArray(), const QString& source = QStringLiteral("Local package"));
	bool removePackage(const QString& pluginId, const QString& version, bool removeSettings, QString& error);
	bool verifyInstalledPlugin(const QString& pluginId, const QString& version, PluginPackageInfo& info, QString& error) const;
	void reload();

private:
	bool ensureSafeRoot(QString& error) const;
	QString root_path;
	QString settings_root;
	PluginStoreCatalogProvider* store_provider; // non-owning; defaults to empty provider
	EmptyPluginStoreCatalogProvider empty_provider;
	bool load_icons = true;
	QList<PluginPackageInfo> installed;
};
