#include "PluginPackage.h"

#include <QtCore/QCryptographicHash>
#include <QtCore/QBuffer>
#include <QtCore/QDir>
#include <QtCore/QDirIterator>
#include <QtCore/QFile>
#include <QtCore/QFileInfo>
#include <QtCore/QHash>
#include <QtCore/QJsonArray>
#include <QtCore/QJsonDocument>
#include <QtCore/QJsonObject>
#include <QtCore/QMap>
#include <QtCore/QRegularExpression>
#include <QtCore/QSaveFile>
#include <QtCore/QSet>
#include <QtCore/QStandardPaths>
#include <QtCore/QUuid>
#include <QtCore/QXmlStreamReader>
#include <QtGui/QImageReader>
#include <QtGui/QPixmap>

#include <algorithm>
#include <limits>

#include <zlib.h>


namespace
{
const quint32 LocalFileSignature = 0x04034b50;
const quint32 CentralDirectorySignature = 0x02014b50;
const quint32 EndOfCentralDirectorySignature = 0x06054b50;
const quint32 DataDescriptorSignature = 0x08074b50;

bool validatePluginSvg(const QByteArray& data, QString& error)
{
	if(data.isEmpty() || data.size() > 64 * 1024)
	{
		error = QStringLiteral("Plugin SVG icons must be non-empty and no larger than 64 KiB.");
		return false;
	}
	QXmlStreamReader reader(data);
	static const QSet<QString> elements = {QStringLiteral("svg"), QStringLiteral("g"), QStringLiteral("path"),
		QStringLiteral("circle"), QStringLiteral("rect"), QStringLiteral("line"), QStringLiteral("polyline"),
		QStringLiteral("polygon"), QStringLiteral("ellipse")};
	static const QSet<QString> attributes = {QStringLiteral("xmlns"), QStringLiteral("width"), QStringLiteral("height"),
		QStringLiteral("viewBox"), QStringLiteral("d"), QStringLiteral("x"), QStringLiteral("y"),
		QStringLiteral("x1"), QStringLiteral("y1"), QStringLiteral("x2"), QStringLiteral("y2"),
		QStringLiteral("cx"), QStringLiteral("cy"), QStringLiteral("r"), QStringLiteral("rx"), QStringLiteral("ry"),
		QStringLiteral("width"), QStringLiteral("height"), QStringLiteral("points"), QStringLiteral("fill"),
		QStringLiteral("stroke"), QStringLiteral("stroke-width"), QStringLiteral("stroke-linecap"),
		QStringLiteral("stroke-linejoin"), QStringLiteral("fill-rule"), QStringLiteral("clip-rule"),
		QStringLiteral("fill-opacity"), QStringLiteral("stroke-opacity"), QStringLiteral("opacity"),
		QStringLiteral("transform")};
	int depth = 0;
	int element_count = 0;
	bool root_seen = false;
	while(!reader.atEnd())
	{
		reader.readNext();
		if(reader.isDTD() || reader.isEntityReference() || reader.isProcessingInstruction())
		{
			error = QStringLiteral("Plugin SVG icons cannot contain DTDs, entities or processing instructions.");
			return false;
		}
		if(reader.isStartElement())
		{
			++depth;
			++element_count;
			const QString name = reader.name().toString();
			const QString namespace_uri = reader.namespaceUri().toString();
			if(depth > 16 || element_count > 128 || !elements.contains(name) ||
				(!root_seen && name != QStringLiteral("svg")) || (name == QStringLiteral("svg") && depth != 1) ||
				(!namespace_uri.isEmpty() && namespace_uri != QStringLiteral("http://www.w3.org/2000/svg")))
			{
				error = QStringLiteral("Plugin SVG icon contains an unsupported element or exceeds its structural limits.");
				return false;
			}
			root_seen = true;
			for(const QXmlStreamAttribute& attribute : reader.attributes())
			{
				const QString attribute_name = attribute.name().toString();
				const QString value = attribute.value().toString();
				const QString lower_value = value.toLower();
				if(!attributes.contains(attribute_name) || lower_value.contains(QStringLiteral("url(")) ||
					lower_value.contains(QStringLiteral("javascript:")) || lower_value.contains(QStringLiteral("http:")) ||
					lower_value.contains(QStringLiteral("https:")) || lower_value.contains(QStringLiteral("data:")) ||
					attribute_name.startsWith(QStringLiteral("on"), Qt::CaseInsensitive) ||
					(attribute_name == QStringLiteral("xmlns") && value != QStringLiteral("http://www.w3.org/2000/svg")))
				{
					error = QStringLiteral("Plugin SVG icon contains a disallowed attribute or external reference.");
					return false;
				}
			}
		}
		else if(reader.isEndElement())
			--depth;
		else if(reader.isCharacters() && !reader.isWhitespace())
		{
			error = QStringLiteral("Plugin SVG icons cannot contain text nodes.");
			return false;
		}
	}
	if(reader.hasError() || !root_seen || depth != 0)
	{
		error = QStringLiteral("Plugin SVG icon is not well-formed XML.");
		return false;
	}
	return true;
}


bool loadPluginRasterIcon(const QByteArray& data, QIcon& icon, QString& error)
{
	if(data.isEmpty() || data.size() > 2 * 1024 * 1024)
	{
		error = QStringLiteral("Plugin icon must be a non-empty image no larger than 2 MiB.");
		return false;
	}
	QBuffer buffer;
	buffer.setData(data);
	if(!buffer.open(QIODevice::ReadOnly))
	{
		error = QStringLiteral("Plugin icon data could not be read.");
		return false;
	}
	QImageReader reader(&buffer);
	reader.setDecideFormatFromContent(true);
	const QByteArray format = reader.format().toLower();
	if(format == QByteArrayLiteral("svg"))
	{
		if(!validatePluginSvg(data, error)) return false;
	}
	const QSize image_size = reader.size();
	if((format != QByteArrayLiteral("png") && format != QByteArrayLiteral("jpeg") && format != QByteArrayLiteral("jpg") &&
		format != QByteArrayLiteral("svg")) ||
		!image_size.isValid() || image_size.width() > 512 || image_size.height() > 512)
	{
		error = QStringLiteral("Plugin icon must be PNG, JPEG or a safe SVG image no larger than 512 by 512 pixels.");
		return false;
	}
	const QImage image = reader.read();
	if(image.isNull())
	{
		error = QStringLiteral("Plugin icon image could not be decoded.");
		return false;
	}
	icon = QIcon(QPixmap::fromImage(image));
	return !icon.isNull();
}

quint16 u16(const QByteArray& bytes, const int offset)
{
	return static_cast<quint16>(static_cast<unsigned char>(bytes[offset])) |
		(static_cast<quint16>(static_cast<unsigned char>(bytes[offset + 1])) << 8);
}

quint32 u32(const QByteArray& bytes, const int offset)
{
	return static_cast<quint32>(u16(bytes, offset)) |
		(static_cast<quint32>(u16(bytes, offset + 2)) << 16);
}

void put16(QByteArray& bytes, const quint16 value)
{
	bytes.append(static_cast<char>(value & 0xff));
	bytes.append(static_cast<char>((value >> 8) & 0xff));
}

void put32(QByteArray& bytes, const quint32 value)
{
	put16(bytes, static_cast<quint16>(value & 0xffff));
	put16(bytes, static_cast<quint16>(value >> 16));
}

bool hasRange(const QByteArray& bytes, const quint64 offset, const quint64 length)
{
	return offset <= static_cast<quint64>(bytes.size()) && length <= static_cast<quint64>(bytes.size()) - offset;
}

bool isCanonicalChildPath(const QString& child_path, const QString& parent_path)
{
	const QString child = QDir::fromNativeSeparators(QDir::cleanPath(child_path));
	const QString parent = QDir::fromNativeSeparators(QDir::cleanPath(parent_path));
	return !child.isEmpty() && !parent.isEmpty() && child.startsWith(parent + QLatin1Char('/'), Qt::CaseInsensitive);
}

QString decodeZipName(const QByteArray& name, const quint16 flags, QString& error)
{
	if(name.contains('\0'))
	{
		error = QStringLiteral("The package contains a ZIP path with a NUL byte.");
		return QString();
	}
	if(flags & (1u << 11))
	{
		const QString decoded = QString::fromUtf8(name.constData(), name.size());
		if(decoded.toUtf8() != name)
		{
			error = QStringLiteral("The package contains an invalid UTF-8 ZIP path.");
			return QString();
		}
		return decoded;
	}
	for(const unsigned char byte : name)
		if(byte >= 0x80)
		{
			error = QStringLiteral("Non-ASCII ZIP paths must be marked as UTF-8.");
			return QString();
		}
	return QString::fromLatin1(name);
}

struct ZipEntry
{
	QString path;
	QByteArray raw_name;
	quint16 flags = 0;
	quint16 method = 0;
	quint32 crc = 0;
	quint32 compressed_size = 0;
	quint32 uncompressed_size = 0;
	quint32 local_offset = 0;
	quint32 external_attributes = 0;
	quint8 creator_system = 0;
	bool directory = false;
	quint64 record_end = 0;
};

bool extractZip(const QString& path, QVector<PluginPackageFile>& files, QByteArray& packageSha256, QString& error)
{
	const QFileInfo package_info(path);
	if(!package_info.isFile() || package_info.isSymLink() || package_info.suffix().compare(QStringLiteral("mspkg"), Qt::CaseInsensitive) != 0)
	{
		error = QStringLiteral("Select a regular .mspkg package file.");
		return false;
	}
	QFile input(path);
	if(!input.open(QIODevice::ReadOnly))
	{
		error = QStringLiteral("Cannot open the plugin package: %1").arg(input.errorString());
		return false;
	}
	if(input.size() <= 0 || input.size() > PluginPackageValidator::MaximumPackageBytes || input.size() > std::numeric_limits<int>::max())
	{
		error = QStringLiteral("The package must be between 1 byte and 64 MiB.");
		return false;
	}
	const QByteArray archive = input.readAll();
	if(archive.size() != input.size())
	{
		error = QStringLiteral("The package could not be read completely.");
		return false;
	}
	packageSha256 = QCryptographicHash::hash(archive, QCryptographicHash::Sha256);

	const int earliest_eocd = std::max(0, archive.size() - 22 - 65535);
	int eocd = -1;
	for(int i = archive.size() - 22; i >= earliest_eocd; --i)
		if(u32(archive, i) == EndOfCentralDirectorySignature && hasRange(archive, i, 22) &&
			static_cast<quint64>(i) + 22 + u16(archive, i + 20) == static_cast<quint64>(archive.size()))
		{
			eocd = i;
			break;
		}
	if(eocd < 0)
	{
		error = QStringLiteral("The package has no valid ZIP end record.");
		return false;
	}
	const quint16 disk_number = u16(archive, eocd + 4);
	const quint16 central_disk = u16(archive, eocd + 6);
	const quint16 disk_entries = u16(archive, eocd + 8);
	const quint16 total_entries = u16(archive, eocd + 10);
	const quint32 central_size = u32(archive, eocd + 12);
	const quint32 central_offset = u32(archive, eocd + 16);
	if(disk_number || central_disk || disk_entries != total_entries || total_entries == 0 ||
		total_entries > PluginPackageValidator::MaximumFileCount || central_offset == 0xffffffffu || central_size == 0xffffffffu ||
		!hasRange(archive, central_offset, central_size) || static_cast<quint64>(central_offset) + central_size != static_cast<quint64>(eocd))
	{
		error = QStringLiteral("Multi-disk, ZIP64, empty, oversized or malformed ZIP packages are not supported.");
		return false;
	}

	QVector<ZipEntry> entries;
	entries.reserve(total_entries);
	QHash<QString, bool> normalized_paths;
	quint64 expanded_total = 0;
	int cursor = static_cast<int>(central_offset);
	for(quint16 i = 0; i < total_entries; ++i)
	{
		if(!hasRange(archive, cursor, 46) || u32(archive, cursor) != CentralDirectorySignature)
		{
			error = QStringLiteral("The ZIP central directory is malformed.");
			return false;
		}
		ZipEntry entry;
		entry.creator_system = static_cast<quint8>(archive[cursor + 5]);
		entry.flags = u16(archive, cursor + 8);
		entry.method = u16(archive, cursor + 10);
		entry.crc = u32(archive, cursor + 16);
		entry.compressed_size = u32(archive, cursor + 20);
		entry.uncompressed_size = u32(archive, cursor + 24);
		const quint16 name_size = u16(archive, cursor + 28);
		const quint16 extra_size = u16(archive, cursor + 30);
		const quint16 comment_size = u16(archive, cursor + 32);
		const quint16 entry_disk = u16(archive, cursor + 34);
		entry.external_attributes = u32(archive, cursor + 38);
		entry.local_offset = u32(archive, cursor + 42);
		const quint64 total_length = 46ULL + name_size + extra_size + comment_size;
		if(!hasRange(archive, cursor, total_length) || name_size == 0 || entry_disk != 0 ||
			entry.compressed_size == 0xffffffffu || entry.uncompressed_size == 0xffffffffu || entry.local_offset == 0xffffffffu)
		{
			error = QStringLiteral("A ZIP entry uses unsupported ZIP64 or malformed fields.");
			return false;
		}
		if((entry.flags & ~static_cast<quint16>(0x080e)) != 0 || (entry.method != 0 && entry.method != 8) ||
			(entry.method == 0 && (entry.flags & 0x0006)) || ((entry.flags & 0x0006) && entry.method != 8))
		{
			error = QStringLiteral("Encrypted or unsupported ZIP compression/options are not allowed.");
			return false;
		}
		entry.raw_name = archive.mid(cursor + 46, name_size);
		entry.path = decodeZipName(entry.raw_name, entry.flags, error);
		if(!error.isEmpty()) return false;
		entry.directory = entry.path.endsWith(QLatin1Char('/'));
		const QString path_error = PluginPackageValidator::pathValidationError(entry.path, entry.directory);
		if(!path_error.isEmpty())	{ error = path_error; return false; }
		if(entry.directory && (entry.compressed_size != 0 || entry.uncompressed_size != 0 || entry.crc != 0))
		{
			error = QStringLiteral("ZIP directory entries must not contain payload data.");
			return false;
		}
		if(entry.method == 0 && entry.compressed_size != entry.uncompressed_size)
		{
			error = QStringLiteral("A stored ZIP entry has inconsistent compressed and uncompressed sizes.");
			return false;
		}
		const QString normalized = (entry.directory ? entry.path.left(entry.path.size() - 1) : entry.path)
			.normalized(QString::NormalizationForm_C).toCaseFolded();
		if(normalized_paths.contains(normalized))
		{
			error = QStringLiteral("The package contains duplicate paths after case/Unicode normalization: %1").arg(entry.path);
			return false;
		}
		for(auto parent = normalized.lastIndexOf(QLatin1Char('/')); parent >= 0; parent = normalized.lastIndexOf(QLatin1Char('/'), parent - 1))
			if(normalized_paths.contains(normalized.left(parent)) && !normalized_paths.value(normalized.left(parent)))
			{
				error = QStringLiteral("A package file is used as a parent directory: %1").arg(entry.path);
				return false;
			}
		if(!entry.directory)
			for(auto child = normalized_paths.constBegin(); child != normalized_paths.constEnd(); ++child)
				if(child.key().startsWith(normalized + QLatin1Char('/')))
				{
					error = QStringLiteral("A package file conflicts with a child path: %1").arg(entry.path);
					return false;
				}
		normalized_paths.insert(normalized, entry.directory);

		const quint32 unix_mode = entry.external_attributes >> 16;
		const quint32 unix_type = unix_mode & 0170000u;
		if((entry.creator_system == 3 && unix_type != 0 && unix_type != (entry.directory ? 0040000u : 0100000u)) ||
			(entry.creator_system == 3 && unix_type == 0120000u) || (entry.external_attributes & 0x400u))
		{
			error = QStringLiteral("Symbolic links and special/reparse-point entries are not allowed in a plugin package.");
			return false;
		}
		if(!entry.directory)
		{
			if(entry.uncompressed_size > PluginPackageValidator::MaximumFileBytes ||
				static_cast<quint64>(entry.compressed_size) > PluginPackageValidator::MaximumPackageBytes ||
				(entry.compressed_size == 0 && entry.uncompressed_size != 0) ||
				(entry.compressed_size > 0 && entry.uncompressed_size > static_cast<quint64>(entry.compressed_size) * 200ULL))
			{
				error = QStringLiteral("A package file exceeds its size or expansion-ratio limit.");
				return false;
			}
			expanded_total += entry.uncompressed_size;
			if(expanded_total > PluginPackageValidator::MaximumExpandedBytes)
			{
				error = QStringLiteral("The expanded plugin package exceeds 64 MiB.");
				return false;
			}
		}
		entries.push_back(entry);
		cursor += static_cast<int>(total_length);
	}
	if(cursor != static_cast<int>(static_cast<quint64>(central_offset) + central_size))
	{
		error = QStringLiteral("The ZIP central-directory size does not match its entries.");
		return false;
	}

	QVector<int> record_order;
	for(int i = 0; i < entries.size(); ++i)
	{
		ZipEntry& entry = entries[i];
		if(!hasRange(archive, entry.local_offset, 30) || u32(archive, entry.local_offset) != LocalFileSignature)
		{
			error = QStringLiteral("A ZIP local-file record is missing or malformed.");
			return false;
		}
		const int local = static_cast<int>(entry.local_offset);
		const quint16 local_flags = u16(archive, local + 6);
		const quint16 local_method = u16(archive, local + 8);
		const quint16 local_name_size = u16(archive, local + 26);
		const quint16 local_extra_size = u16(archive, local + 28);
		const quint64 data_offset = static_cast<quint64>(local) + 30 + local_name_size + local_extra_size;
		if(local_flags != entry.flags || local_method != entry.method || local_name_size != entry.raw_name.size() ||
			!hasRange(archive, local + 30, local_name_size + local_extra_size) || archive.mid(local + 30, local_name_size) != entry.raw_name ||
			!hasRange(archive, data_offset, entry.compressed_size) || data_offset + entry.compressed_size > central_offset)
		{
			error = QStringLiteral("ZIP local and central entry records disagree.");
			return false;
		}
		quint64 end = data_offset + entry.compressed_size;
		if(entry.flags & (1u << 3))
		{
			if(hasRange(archive, end, 4) && u32(archive, static_cast<int>(end)) == DataDescriptorSignature) end += 4;
			if(!hasRange(archive, end, 12) || u32(archive, static_cast<int>(end)) != entry.crc ||
				u32(archive, static_cast<int>(end + 4)) != entry.compressed_size ||
				u32(archive, static_cast<int>(end + 8)) != entry.uncompressed_size)
			{
				error = QStringLiteral("A ZIP data descriptor does not match its central entry.");
				return false;
			}
			end += 12;
		}
		else if(u32(archive, local + 14) != entry.crc || u32(archive, local + 18) != entry.compressed_size ||
			u32(archive, local + 22) != entry.uncompressed_size)
		{
			error = QStringLiteral("ZIP local sizes/checksum do not match its central entry.");
			return false;
		}
		entry.record_end = end;
		record_order.push_back(i);
	}
	std::sort(record_order.begin(), record_order.end(), [&entries](const int a, const int b) {
		return entries[a].local_offset < entries[b].local_offset;
	});
	quint64 previous_end = 0;
	for(const int index : record_order)
	{
		const ZipEntry& entry = entries[index];
		if(entry.local_offset < previous_end)
		{
			error = QStringLiteral("ZIP entry records overlap or share a local offset.");
			return false;
		}
		previous_end = entry.record_end;
	}

	files.clear();
	files.reserve(entries.size());
	for(const ZipEntry& entry : entries)
	{
		if(entry.directory) continue;
		const int local = static_cast<int>(entry.local_offset);
		const quint64 data_offset = static_cast<quint64>(local) + 30 + u16(archive, local + 26) + u16(archive, local + 28);
		const QByteArray compressed = archive.mid(static_cast<int>(data_offset), static_cast<int>(entry.compressed_size));
		QByteArray contents;
		contents.resize(static_cast<int>(entry.uncompressed_size));
		if(entry.method == 0)
			contents = compressed;
		else
		{
			z_stream stream = {};
			stream.next_in = reinterpret_cast<Bytef*>(const_cast<char*>(compressed.constData()));
			stream.avail_in = static_cast<uInt>(compressed.size());
				char empty_output = 0;
				stream.next_out = contents.isEmpty() ? reinterpret_cast<Bytef*>(&empty_output) : reinterpret_cast<Bytef*>(contents.data());
				stream.avail_out = contents.isEmpty() ? 1 : static_cast<uInt>(contents.size());
			if(inflateInit2(&stream, -MAX_WBITS) != Z_OK)
			{
				error = QStringLiteral("Could not initialize the ZIP decompressor.");
				return false;
			}
			const int result = inflate(&stream, Z_FINISH);
			const bool valid = result == Z_STREAM_END && stream.total_in == entry.compressed_size &&
				stream.total_out == entry.uncompressed_size;
			inflateEnd(&stream);
			if(!valid)
			{
				error = QStringLiteral("A compressed package file is truncated or invalid: %1").arg(entry.path);
				return false;
			}
		}
		const quint32 actual_crc = static_cast<quint32>(crc32(0, reinterpret_cast<const Bytef*>(contents.constData()), static_cast<uInt>(contents.size())));
		if(actual_crc != entry.crc)
		{
			error = QStringLiteral("CRC verification failed for package file: %1").arg(entry.path);
			return false;
		}
		PluginPackageFile file;
		file.path = entry.path;
		file.contents = contents;
		file.sha256 = QCryptographicHash::hash(contents, QCryptographicHash::Sha256).toHex();
		files.push_back(file);
	}
	return true;
}

bool writeJsonAtomically(const QString& path, const QJsonObject& object, QString& error)
{
	QSaveFile file(path);
	const QByteArray data = QJsonDocument(object).toJson(QJsonDocument::Indented);
	if(!file.open(QIODevice::WriteOnly) || file.write(data) != data.size() || !file.commit())
	{
		error = QStringLiteral("Cannot write plugin installation metadata: %1").arg(file.errorString());
		return false;
	}
	return true;
}

bool validVersion(const QString& version)
{
	if(version.isEmpty() || version.size() > 80) return false;
	QString without_build = version;
	const int build_separator = without_build.indexOf(QLatin1Char('+'));
	if(build_separator >= 0)
	{
		if(without_build.indexOf(QLatin1Char('+'), build_separator + 1) >= 0) return false;
		const QString build = without_build.mid(build_separator + 1);
		for(const QString& identifier : build.split(QLatin1Char('.'), Qt::KeepEmptyParts))
		{
			if(identifier.isEmpty()) return false;
			for(const QChar c : identifier)
				if(!(c.isLetterOrNumber() && c.unicode() <= 0x7f) && c != QLatin1Char('-')) return false;
		}
		without_build.truncate(build_separator);
	}
	QString core = without_build;
	QString prerelease;
	const int prerelease_separator = without_build.indexOf(QLatin1Char('-'));
	if(prerelease_separator >= 0)
	{
		core = without_build.left(prerelease_separator);
		prerelease = without_build.mid(prerelease_separator + 1);
		if(prerelease.isEmpty()) return false;
	}
	const QStringList core_identifiers = core.split(QLatin1Char('.'), Qt::KeepEmptyParts);
	if(core_identifiers.size() != 3) return false;
	for(const QString& identifier : core_identifiers)
	{
		if(identifier.isEmpty() || (identifier.size() > 1 && identifier.startsWith(QLatin1Char('0')))) return false;
		for(const QChar c : identifier) if(!c.isDigit() || c.unicode() > 0x7f) return false;
	}
	if(!prerelease.isEmpty())
		for(const QString& identifier : prerelease.split(QLatin1Char('.'), Qt::KeepEmptyParts))
		{
			if(identifier.isEmpty()) return false;
			bool numeric = true;
			bool has_letter_or_hyphen = false;
			for(const QChar c : identifier)
			{
				if(!(c.isLetterOrNumber() && c.unicode() <= 0x7f) && c != QLatin1Char('-')) return false;
				if(!c.isDigit()) numeric = false;
				if(c.isLetter() || c == QLatin1Char('-')) has_letter_or_hyphen = true;
			}
			if(numeric && identifier.size() > 1 && identifier.startsWith(QLatin1Char('0'))) return false;
			if(!numeric && !has_letter_or_hyphen) return false;
		}
	return true;
}


int compareNumericIdentifiers(const QString& left, const QString& right)
{
	if(left.size() != right.size()) return left.size() < right.size() ? -1 : 1;
	const int comparison = QString::compare(left, right, Qt::CaseSensitive);
	return comparison < 0 ? -1 : comparison > 0 ? 1 : 0;
}


QStringList versionPrerelease(const QString& version)
{
	const QString without_build = version.section(QLatin1Char('+'), 0, 0);
	const int separator = without_build.indexOf(QLatin1Char('-'));
	return separator < 0 ? QStringList() : without_build.mid(separator + 1).split(QLatin1Char('.'));
}


bool isNumericPrereleaseIdentifier(const QString& identifier)
{
	if(identifier.isEmpty()) return false;
	for(const QChar c : identifier) if(!c.isDigit() || c.unicode() > 0x7f) return false;
	return true;
}
}


QString PluginPackageValidator::pathValidationError(const QString& path, const bool allowDirectory)
{
	if(path.isEmpty() || path.size() > 240 || path.startsWith(QLatin1Char('/')) || path.contains(QLatin1Char('\\')) ||
		path.contains(QLatin1Char(':')) || path.contains(QChar::Null) || path != path.normalized(QString::NormalizationForm_C))
		return QStringLiteral("Unsafe or non-canonical package path: %1").arg(path.left(100));
	const QString path_without_trailing_slash = path.endsWith(QLatin1Char('/')) ? path.left(path.size() - 1) : path;
	if(path_without_trailing_slash.compare(QStringLiteral(".metasiberia-install.json"), Qt::CaseInsensitive) == 0)
		return QStringLiteral("The package path is reserved for Metasiberia installation metadata.");
	const bool is_directory = path.endsWith(QLatin1Char('/'));
	if(is_directory && !allowDirectory)
		return QStringLiteral("A file path cannot end in '/': %1").arg(path.left(100));
	const QString body = is_directory ? path.left(path.size() - 1) : path;
	const QStringList components = body.split(QLatin1Char('/'), Qt::KeepEmptyParts);
	static const QRegularExpression reserved(QStringLiteral("^(CON|PRN|AUX|NUL|COM[1-9]|LPT[1-9])(?:\\..*)?$"), QRegularExpression::CaseInsensitiveOption);
	for(const QString& component : components)
	{
		if(component.isEmpty() || component == QStringLiteral(".") || component == QStringLiteral("..") ||
			component.endsWith(QLatin1Char('.')) || component.endsWith(QLatin1Char(' ')) || reserved.match(component).hasMatch())
			return QStringLiteral("Unsafe package path component in: %1").arg(path.left(100));
		for(const QChar c : component)
			if(c.category() == QChar::Other_Control)
				return QStringLiteral("Control characters are not allowed in package paths.");
	}
	return QString();
}


int PluginPackageValidator::compareVersions(const QString& left, const QString& right)
{
	if(left == right) return 0;
	if(!validVersion(left) || !validVersion(right)) return 0;
	const QString left_core = left.section(QLatin1Char('-'), 0, 0).section(QLatin1Char('+'), 0, 0);
	const QString right_core = right.section(QLatin1Char('-'), 0, 0).section(QLatin1Char('+'), 0, 0);
	const QStringList left_core_parts = left_core.split(QLatin1Char('.'));
	const QStringList right_core_parts = right_core.split(QLatin1Char('.'));
	for(int i = 0; i < 3; ++i)
	{
		const int comparison = compareNumericIdentifiers(left_core_parts[i], right_core_parts[i]);
		if(comparison != 0) return comparison;
	}
	const QStringList left_prerelease = versionPrerelease(left);
	const QStringList right_prerelease = versionPrerelease(right);
	if(left_prerelease.isEmpty()) return right_prerelease.isEmpty() ? 0 : 1;
	if(right_prerelease.isEmpty()) return -1;
	const int common = std::min(left_prerelease.size(), right_prerelease.size());
	for(int i = 0; i < common; ++i)
	{
		const bool left_numeric = isNumericPrereleaseIdentifier(left_prerelease[i]);
		const bool right_numeric = isNumericPrereleaseIdentifier(right_prerelease[i]);
		if(left_numeric && right_numeric)
		{
			const int comparison = compareNumericIdentifiers(left_prerelease[i], right_prerelease[i]);
			if(comparison != 0) return comparison;
		}
		else if(left_numeric != right_numeric)
			return left_numeric ? -1 : 1;
		else
		{
			const int comparison = QString::compare(left_prerelease[i], right_prerelease[i], Qt::CaseSensitive);
			if(comparison != 0) return comparison < 0 ? -1 : 1;
		}
	}
	return left_prerelease.size() < right_prerelease.size() ? -1 :
		left_prerelease.size() > right_prerelease.size() ? 1 : 0;
}


bool PluginStoreCatalogProvider::downloadPackage(const PluginPackageInfo&, const QString&, QString& error) const
{
	error = QStringLiteral("This catalog provider does not support package downloads.");
	return false;
}


bool PluginPackageValidator::parseManifest(const QByteArray& data, const QStringList& packageFiles,
	PluginPackageInfo& info, QString& error)
{
	if(data.size() <= 0 || data.size() > MaximumManifestBytes)
	{
		error = QStringLiteral("plugin.json must be between 1 byte and 64 KiB.");
		return false;
	}
	QJsonParseError parse_error;
	const QJsonDocument document = QJsonDocument::fromJson(data, &parse_error);
	if(parse_error.error != QJsonParseError::NoError || !document.isObject())
	{
		error = QStringLiteral("plugin.json is invalid JSON: %1").arg(parse_error.errorString());
		return false;
	}
	const QJsonObject object = document.object();
	if(object.value(QStringLiteral("schemaVersion")).toInt(-1) != 1)
	{
		error = QStringLiteral("Unsupported plugin.json schemaVersion (expected 1).");
		return false;
	}
	const auto stringField = [&object](const QString& name, const int maximum, QString& output) -> bool {
		if(!object.value(name).isString()) return false;
		output = object.value(name).toString().trimmed();
		return !output.isEmpty() && output.size() <= maximum;
	};
	if(!stringField(QStringLiteral("id"), 160, info.id) ||
		!stringField(QStringLiteral("name"), 120, info.name) ||
		!stringField(QStringLiteral("author"), 120, info.author) ||
		!stringField(QStringLiteral("version"), 80, info.version) ||
		!stringField(QStringLiteral("runtime"), 48, info.runtime) ||
		!stringField(QStringLiteral("entry"), 240, info.entryPoint))
	{
		error = QStringLiteral("plugin.json is missing a required bounded string field (id, name, author, version, runtime or entry).");
		return false;
	}
	static const QRegularExpression runtime_pattern(QStringLiteral("^[A-Za-z0-9][A-Za-z0-9._-]{0,47}$"));
	if(!runtime_pattern.match(info.runtime).hasMatch())
	{
		error = QStringLiteral("The runtime identifier is malformed.");
		return false;
	}
	static const QRegularExpression id_pattern(QStringLiteral("^[a-z0-9]+(?:[.-][a-z0-9]+)*(?:\\.[a-z0-9]+(?:-[a-z0-9]+)*)+$"));
	if(!id_pattern.match(info.id).hasMatch() || !validVersion(info.version))
	{
		error = QStringLiteral("Plugin id must be reverse-domain lowercase text and version must use semantic version format.");
		return false;
	}
	if(const QString path_error = pathValidationError(info.entryPoint); !path_error.isEmpty())
	{
		error = QStringLiteral("Invalid plugin entry point: %1").arg(path_error);
		return false;
	}
	if(!packageFiles.contains(QStringLiteral("plugin.json")) || !packageFiles.contains(info.entryPoint))
	{
		error = QStringLiteral("plugin.json or its declared entry file is missing from the package.");
		return false;
	}
	if(object.contains(QStringLiteral("icon")))
	{
		if(!object.value(QStringLiteral("icon")).isString())
		{
			error = QStringLiteral("icon must be a package-relative PNG, JPEG or SVG file path.");
			return false;
		}
		info.iconPath = object.value(QStringLiteral("icon")).toString().trimmed();
		if(const QString path_error = pathValidationError(info.iconPath); !path_error.isEmpty() ||
			(!info.iconPath.endsWith(QStringLiteral(".png"), Qt::CaseInsensitive) &&
			 !info.iconPath.endsWith(QStringLiteral(".jpg"), Qt::CaseInsensitive) &&
			 !info.iconPath.endsWith(QStringLiteral(".jpeg"), Qt::CaseInsensitive) &&
			 !info.iconPath.endsWith(QStringLiteral(".svg"), Qt::CaseInsensitive)) ||
			!packageFiles.contains(info.iconPath))
		{
			error = QStringLiteral("icon must refer to an included PNG, JPEG or safe SVG package file with a relative path.");
			return false;
		}
	}
	if(object.contains(QStringLiteral("description")) &&
		(!object.value(QStringLiteral("description")).isString() || object.value(QStringLiteral("description")).toString().size() > 2000))
	{
		error = QStringLiteral("description must be a string no longer than 2,000 characters.");
		return false;
	}
	info.description = object.value(QStringLiteral("description")).toString();
	if(!object.value(QStringLiteral("api")).isObject())
	{
		error = QStringLiteral("plugin.json must include an api object with major and minor integers.");
		return false;
	}
	const QJsonObject api = object.value(QStringLiteral("api")).toObject();
	if(!api.value(QStringLiteral("major")).isDouble() || !api.value(QStringLiteral("minor")).isDouble() ||
		api.value(QStringLiteral("major")).toInt(-1) < 0 || api.value(QStringLiteral("minor")).toInt(-1) < 0 ||
		api.value(QStringLiteral("major")).toDouble() != api.value(QStringLiteral("major")).toInt() ||
		api.value(QStringLiteral("minor")).toDouble() != api.value(QStringLiteral("minor")).toInt())
	{
		error = QStringLiteral("Plugin API major/minor must be non-negative integers.");
		return false;
	}
	info.apiMajor = api.value(QStringLiteral("major")).toInt();
	info.apiMinor = api.value(QStringLiteral("minor")).toInt();
	info.apiCompatible = info.apiMajor == CurrentApiMajor && info.apiMinor <= CurrentApiMinor;
	const QJsonValue permissions_value = object.value(QStringLiteral("permissions"));
	if(!permissions_value.isArray() || permissions_value.toArray().size() > 32)
	{
		error = QStringLiteral("permissions must be an array with at most 32 entries.");
		return false;
	}
	QSet<QString> permission_set;
	static const QRegularExpression permission_pattern(QStringLiteral("^[a-z][a-z0-9]*(?:[.-][a-z0-9]+)*$") );
	for(const QJsonValue& permission_value : permissions_value.toArray())
	{
		if(!permission_value.isString())
		{
			error = QStringLiteral("Every permission must be a string.");
			return false;
		}
		const QString permission = permission_value.toString();
		if(permission.size() > 96 || !permission_pattern.match(permission).hasMatch() || permission_set.contains(permission))
		{
			error = QStringLiteral("A permission is invalid or duplicated: %1").arg(permission.left(96));
			return false;
		}
		permission_set.insert(permission);
		info.permissions.push_back(permission);
	}
	const auto parseStringArray = [&object](const QString& key, QStringList& target, const int limit, const int item_limit) -> bool {
		if(!object.contains(key)) return true;
		const QJsonValue value = object.value(key);
		if(!value.isArray() || value.toArray().size() > limit) return false;
		for(const QJsonValue& item : value.toArray())
			if(!item.isString() || item.toString().size() > item_limit) return false;
			else target.push_back(item.toString());
		return true;
	};
	if(!parseStringArray(QStringLiteral("categories"), info.categories, 16, 64))
	{
		error = QStringLiteral("categories must be an array of bounded strings.");
		return false;
	}
	if(object.contains(QStringLiteral("dependencies")) && !object.value(QStringLiteral("dependencies")).isArray())
	{
		error = QStringLiteral("dependencies must be an array.");
		return false;
	}
	if(object.value(QStringLiteral("dependencies")).toArray().size() > 0)
	{
		error = QStringLiteral("Plugin dependencies are declared, but dependency installation is not available in this version.");
		return false;
	}
	return true;
}


bool PluginPackageValidator::readPackage(const QString& packagePath, PluginPackageInfo& info,
	QVector<PluginPackageFile>& files, QString& error)
{
	error.clear();
	info = PluginPackageInfo();
	QByteArray package_sha256;
	if(!extractZip(packagePath, files, package_sha256, error)) return false;
	QStringList paths;
	const PluginPackageFile* manifest = nullptr;
	for(const PluginPackageFile& file : files)
	{
		paths.push_back(file.path);
		if(file.path == QStringLiteral("plugin.json")) manifest = &file;
	}
	if(!manifest)
	{
		error = QStringLiteral("Package root must contain plugin.json.");
		return false;
	}
	if(!parseManifest(manifest->contents, paths, info, error)) return false;
	if(!info.iconPath.isEmpty())
	{
		for(const PluginPackageFile& file : files)
			if(file.path == info.iconPath && !loadPluginRasterIcon(file.contents, info.icon, error))
				return false;
	}
	info.packageSha256 = package_sha256;
	return true;
}


bool PluginPackageValidator::inspect(const QString& packagePath, PluginPackageInfo& info, QString& error)
{
	QVector<PluginPackageFile> files;
	return readPackage(packagePath, info, files, error);
}


bool PluginPackageValidator::readInstalledDirectory(const QString& directoryPath, PluginPackageInfo& info,
	QString& error, const bool loadIcon)
{
	error.clear();
	const QFileInfo directory_info(directoryPath);
	if(!directory_info.isDir() || directory_info.isSymLink())
	{
		error = QStringLiteral("Installed plugin path is not a regular directory.");
		return false;
	}
	QFile metadata_file(QDir(directoryPath).filePath(QStringLiteral(".metasiberia-install.json")));
	if(QFileInfo(metadata_file.fileName()).isSymLink() || !metadata_file.open(QIODevice::ReadOnly) || metadata_file.size() > MaximumManifestBytes)
	{
		error = QStringLiteral("Installed package metadata is missing or too large.");
		return false;
	}
	QJsonParseError parse_error;
	const QJsonDocument metadata_document = QJsonDocument::fromJson(metadata_file.readAll(), &parse_error);
	if(parse_error.error != QJsonParseError::NoError || !metadata_document.isObject())
	{
		error = QStringLiteral("Installed package metadata is invalid JSON.");
		return false;
	}
	const QJsonObject metadata = metadata_document.object();
	if(metadata.value(QStringLiteral("schemaVersion")).toInt(-1) != 1)
	{
		error = QStringLiteral("Unsupported installed-package metadata schema.");
		return false;
	}
	const QJsonObject hashes = metadata.value(QStringLiteral("files")).toObject();
	if(hashes.isEmpty() || hashes.size() > MaximumFileCount)
	{
		error = QStringLiteral("Installed package file-hash manifest is empty or oversized.");
		return false;
	}
	QDir root(directoryPath);
	QSet<QString> seen;
	QMap<QString, QByteArray> actual_hashes;
	QDirIterator iterator(directoryPath, QDir::AllEntries | QDir::NoDotAndDotDot, QDirIterator::Subdirectories);
	while(iterator.hasNext())
	{
		const QString absolute = iterator.next();
		const QFileInfo file_info = iterator.fileInfo();
		if(file_info.isSymLink())
		{
			error = QStringLiteral("Installed package contains a symbolic link or non-file entry.");
			return false;
		}
		if(file_info.isDir()) continue;
		if(!file_info.isFile())
		{
			error = QStringLiteral("Installed package contains a special filesystem entry.");
			return false;
		}
		const QString relative = root.relativeFilePath(absolute).replace(QLatin1Char('\\'), QLatin1Char('/'));
		if(relative == QStringLiteral(".metasiberia-install.json")) continue;
		const QString path_error = pathValidationError(relative);
		if(!path_error.isEmpty() || !hashes.contains(relative))
		{
			error = QStringLiteral("Installed package contains an unexpected path: %1").arg(relative.left(100));
			return false;
		}
		QFile file(absolute);
		if(!file.open(QIODevice::ReadOnly) || file.size() > MaximumFileBytes)
		{
			error = QStringLiteral("Installed package file is unavailable or oversized: %1").arg(relative);
			return false;
		}
		const QByteArray contents = file.readAll();
		const QByteArray digest = QCryptographicHash::hash(contents, QCryptographicHash::Sha256).toHex();
		if(digest != hashes.value(relative).toString().toLatin1())
		{
			error = QStringLiteral("Installed package checksum mismatch: %1").arg(relative);
			return false;
		}
		seen.insert(relative);
		actual_hashes.insert(relative, digest);
	}
	if(seen.size() != hashes.size())
	{
		error = QStringLiteral("Installed package is missing one or more verified files.");
		return false;
	}
	QCryptographicHash installed_content_hash(QCryptographicHash::Sha256);
	for(auto it = actual_hashes.constBegin(); it != actual_hashes.constEnd(); ++it)
	{
		installed_content_hash.addData(it.key().toUtf8());
		installed_content_hash.addData("\n", 1);
		installed_content_hash.addData(it.value());
		installed_content_hash.addData("\n", 1);
	}
	info.installedContentSha256 = installed_content_hash.result();
	QFile manifest_file(QDir(directoryPath).filePath(QStringLiteral("plugin.json")));
	if(!manifest_file.open(QIODevice::ReadOnly))
	{
		error = QStringLiteral("Installed package manifest is missing.");
		return false;
	}
	QStringList paths = seen.values();
	if(!parseManifest(manifest_file.readAll(), paths, info, error)) return false;
	if(loadIcon && !info.iconPath.isEmpty())
	{
		QFile icon_file(QDir(directoryPath).filePath(info.iconPath));
		if(!icon_file.open(QIODevice::ReadOnly) || icon_file.size() > 2 * 1024 * 1024)
		{
			error = QStringLiteral("Installed plugin icon is unavailable or oversized.");
			return false;
		}
		if(!loadPluginRasterIcon(icon_file.readAll(), info.icon, error))
			return false;
	}
	if(metadata.value(QStringLiteral("id")).toString() != info.id || metadata.value(QStringLiteral("version")).toString() != info.version)
	{
		error = QStringLiteral("Installed package metadata does not match plugin.json.");
		return false;
	}
	const QString recorded_package_sha256 = metadata.value(QStringLiteral("packageSha256")).toString();
	if(!recorded_package_sha256.isEmpty())
	{
		static const QRegularExpression digest_pattern(QStringLiteral("^[0-9a-f]{64}$"));
		static const QRegularExpression legacy_digest_pattern(QStringLiteral("^[0-9a-f]{128}$"));
		if(!digest_pattern.match(recorded_package_sha256).hasMatch() &&
			!legacy_digest_pattern.match(recorded_package_sha256).hasMatch())
		{
			error = QStringLiteral("Installed package identity hash is malformed.");
			return false;
		}
		info.packageSha256 = QByteArray::fromHex(recorded_package_sha256.toLatin1());
		// Earlier builds stored the 64 ASCII hex characters as bytes, then
		// hex-encoded them in metadata (128 characters). Normalize that format.
		if(legacy_digest_pattern.match(recorded_package_sha256).hasMatch() &&
			digest_pattern.match(QString::fromLatin1(info.packageSha256)).hasMatch())
			info.packageSha256 = QByteArray::fromHex(info.packageSha256);
		if(info.packageSha256.size() != 32)
		{
			error = QStringLiteral("Installed package identity hash has an unsupported length.");
			return false;
		}
	}
	else
	{
		// Older inert installations predate the archive digest field. Derive a
		// stable identity from the already verified sorted per-file hashes; the
		// first explicit enable still requires owner consent for this identity.
		QStringList file_names = hashes.keys();
		file_names.sort(Qt::CaseSensitive);
		QByteArray canonical_hashes;
		for(const QString& file_name : file_names)
		{
			canonical_hashes += file_name.toUtf8();
			canonical_hashes += '\n';
			canonical_hashes += hashes.value(file_name).toString().toLatin1();
			canonical_hashes += '\n';
		}
		info.packageSha256 = QCryptographicHash::hash(canonical_hashes, QCryptographicHash::Sha256);
	}
	info.installed = true;
	info.enabled = false;
	info.running = false;
	info.state = QStringLiteral("Installed — disabled");
	info.source = metadata.value(QStringLiteral("source")).toString(QStringLiteral("Local package"));
	info.installPath = directory_info.absoluteFilePath();
	info.installedAt = QDateTime::fromString(metadata.value(QStringLiteral("installedUtc")).toString(), Qt::ISODateWithMs);
	return true;
}


QString EmptyPluginStoreCatalogProvider::statusText() const
{
	return QStringLiteral("The plugin store service is not configured. No published plugins are available.");
}


QList<PluginPackageInfo> EmptyPluginStoreCatalogProvider::search(const QString&, QString& error) const
{
	error = QString();
	return QList<PluginPackageInfo>();
}


PluginPackageManager::PluginPackageManager(const QString& rootDirectory, PluginStoreCatalogProvider* provider,
	const QString& settingsRoot, const bool loadIcons)
: root_path(rootDirectory), settings_root(settingsRoot), store_provider(provider), load_icons(loadIcons)
{
	if(root_path.isEmpty())
	{
		QString app_data = QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation);
		if(app_data.isEmpty()) app_data = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
		root_path = QDir(app_data).filePath(QStringLiteral("plugins"));
	}
	if(settings_root.isEmpty())
	{
		settings_root = QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation);
		if(settings_root.isEmpty()) settings_root = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
	}
	if(!store_provider) store_provider = &empty_provider;
	reload();
}


QString PluginPackageManager::rootDirectory() const { return QDir::cleanPath(root_path); }
QString PluginPackageManager::settingsRootDirectory() const { return QDir::cleanPath(settings_root); }


QString PluginPackageManager::settingsDirectoryForId(const QString& pluginId) const
{
	static const QRegularExpression id_pattern(QStringLiteral("^[a-z0-9]+(?:[.-][a-z0-9]+)*(?:\\.[a-z0-9]+(?:-[a-z0-9]+)*)+$"));
	if(!id_pattern.match(pluginId).hasMatch()) return QString();
	return QDir(QDir(settings_root).filePath(QStringLiteral("plugin-settings"))).filePath(pluginId);
}


QList<PluginPackageInfo> PluginPackageManager::installedPlugins() const { return installed; }


bool PluginPackageManager::verifyInstalledPlugin(const QString& pluginId, const QString& version,
	PluginPackageInfo& info, QString& error) const
{
	for(const PluginPackageInfo& candidate : installed)
		if(candidate.id == pluginId && candidate.version == version)
		{
			if(!PluginPackageValidator::readInstalledDirectory(candidate.installPath, info, error))
				return false;
			if(info.id == pluginId && info.version == version)
				return true;
			error = QStringLiteral("The selected plugin directory does not match its installed identity.");
			return false;
		}
	error = QStringLiteral("The selected plugin version is not installed.");
	return false;
}


QList<PluginPackageInfo> PluginPackageManager::storePlugins(const QString& query, QString& error) const
{
	return store_provider->search(query, error);
}


QString PluginPackageManager::storeStatusText() const { return store_provider->statusText(); }


bool PluginPackageManager::canDownloadStorePackage(const PluginPackageInfo& info) const
{
	return store_provider->isAvailable() && store_provider->canDownload(info);
}


bool PluginPackageManager::downloadStorePackage(const PluginPackageInfo& info, const QString& destinationPath, QString& error) const
{
	if(!canDownloadStorePackage(info))
	{
		error = QStringLiteral("The selected catalog entry has no available package download.");
		return false;
	}
	return store_provider->downloadPackage(info, destinationPath, error);
}


bool PluginPackageManager::inspectPackage(const QString& packagePath, PluginPackageInfo& info, QString& error) const
{
	return PluginPackageValidator::inspect(packagePath, info, error);
}


bool PluginPackageManager::ensureSafeRoot(QString& error) const
{
	const QFileInfo root_info(root_path);
	if(root_info.exists() && (!root_info.isDir() || root_info.isSymLink()))
	{
		error = QStringLiteral("The plugin directory is not a regular directory; installation was refused.");
		return false;
	}
	if(!root_info.exists() && !QDir().mkpath(root_path))
	{
		error = QStringLiteral("Cannot create the user plugin directory: %1").arg(root_path);
		return false;
	}
	return true;
}


bool PluginPackageManager::installPackage(const QString& packagePath, PluginPackageInfo& installedInfo, QString& error,
	const QByteArray& expectedPackageSha256, const QString& source)
{
	QVector<PluginPackageFile> files;
	PluginPackageInfo info;
	if(!PluginPackageValidator::readPackage(packagePath, info, files, error)) return false;
	if(!expectedPackageSha256.isEmpty() && info.packageSha256 != expectedPackageSha256)
	{
		error = QStringLiteral("The package changed after review. Select and review it again before installation.");
		return false;
	}
	if(!info.apiCompatible)
	{
		error = QStringLiteral("This package targets Plugin API %1.%2; this client supports %3.%4.")
			.arg(info.apiMajor).arg(info.apiMinor).arg(PluginPackageValidator::CurrentApiMajor).arg(PluginPackageValidator::CurrentApiMinor);
		return false;
	}
	if(!ensureSafeRoot(error)) return false;
	const QString install_parent = QDir(root_path).filePath(info.id);
	const QFileInfo parent_info(install_parent);
	if(parent_info.exists() && (!parent_info.isDir() || parent_info.isSymLink()))
	{
		error = QStringLiteral("The existing plugin id path is not a regular directory.");
		return false;
	}
	if(!QDir().mkpath(install_parent))
	{
		error = QStringLiteral("Cannot create the plugin install directory.");
		return false;
	}
	const QString final_path = QDir(install_parent).filePath(info.version);
	QVector<QPair<QString, QString>> previous_versions;
	QString newest_installed_version;
	const QStringList installed_version_names = QDir(install_parent).entryList(QDir::Dirs | QDir::NoDotAndDotDot, QDir::Name);
	for(const QString& old_version : installed_version_names)
	{
		const QString old_path = QDir(install_parent).filePath(old_version);
		if(QFileInfo(old_path).isSymLink())
		{
			error = QStringLiteral("An installed version path is a symbolic link; update was refused.");
			return false;
		}
		PluginPackageInfo old_info;
		if(!PluginPackageValidator::readInstalledDirectory(old_path, old_info, error, false) ||
			old_info.id != info.id || old_info.version != old_version)
		{
			if(error.isEmpty()) error = QStringLiteral("An existing package directory failed identity validation; update was refused.");
			return false;
		}
		if(old_info.version == info.version)
		{
			error = old_info.packageSha256 == info.packageSha256
				? QStringLiteral("Plugin %1 version %2 with this package hash is already installed.").arg(info.id, info.version)
				: QStringLiteral("A different package hash for plugin %1 version %2 is refused; increase the version before updating.").arg(info.id, info.version);
			return false;
		}
		if(newest_installed_version.isEmpty() || PluginPackageValidator::compareVersions(old_info.version, newest_installed_version) > 0)
			newest_installed_version = old_info.version;
		previous_versions.push_back(qMakePair(old_version, old_path));
	}
	if(!newest_installed_version.isEmpty() && PluginPackageValidator::compareVersions(info.version, newest_installed_version) < 0)
	{
		error = QStringLiteral("Installing an older plugin version is refused; install %1 or a newer version.").arg(newest_installed_version);
		return false;
	}
	const QString staging_parent = QDir(root_path).filePath(QStringLiteral(".staging"));
	const QFileInfo existing_staging_info(staging_parent);
	if(existing_staging_info.exists() && (!existing_staging_info.isDir() || existing_staging_info.isSymLink()))
	{
		error = QStringLiteral("The package staging path is not a regular directory.");
		return false;
	}
	if(!QDir().mkpath(staging_parent))
	{
		error = QStringLiteral("Cannot create the same-volume staging directory.");
		return false;
	}
	const QString root_canonical = QFileInfo(root_path).canonicalFilePath();
	const QString staging_canonical = QFileInfo(staging_parent).canonicalFilePath();
	if(!isCanonicalChildPath(staging_canonical, root_canonical))
	{
		error = QStringLiteral("The canonical staging path is outside the plugin data directory.");
		return false;
	}
	const QString stage_path = QDir(staging_parent).filePath(QUuid::createUuid().toString(QUuid::WithoutBraces));
	if(QFileInfo::exists(stage_path) || !QDir().mkdir(stage_path))
	{
		error = QStringLiteral("Cannot create plugin staging directory.");
		return false;
	}
	const QString stage_canonical = QFileInfo(stage_path).canonicalFilePath();
	if(!isCanonicalChildPath(stage_canonical, staging_canonical))
	{
		QDir(stage_path).removeRecursively();
		error = QStringLiteral("The canonical package staging directory is outside its verified parent.");
		return false;
	}
	QJsonObject hash_map;
	bool write_ok = true;
	for(const PluginPackageFile& file : files)
	{
		const QString destination = QDir(stage_path).filePath(file.path);
		if(!QDir().mkpath(QFileInfo(destination).absolutePath()))
		{
			error = QStringLiteral("Cannot create a package subdirectory: %1").arg(file.path);
			write_ok = false;
			break;
		}
		QSaveFile output(destination);
		if(!output.open(QIODevice::WriteOnly) || output.write(file.contents) != file.contents.size() || !output.commit())
		{
			error = QStringLiteral("Cannot write package file %1: %2").arg(file.path, output.errorString());
			write_ok = false;
			break;
		}
		hash_map.insert(file.path, QString::fromLatin1(file.sha256));
	}
	if(write_ok)
	{
		QJsonObject metadata;
		metadata.insert(QStringLiteral("schemaVersion"), 1);
		metadata.insert(QStringLiteral("id"), info.id);
		metadata.insert(QStringLiteral("version"), info.version);
		metadata.insert(QStringLiteral("source"), source);
		metadata.insert(QStringLiteral("packageSha256"), QString::fromLatin1(info.packageSha256.toHex()));
		metadata.insert(QStringLiteral("installedUtc"), QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs));
		metadata.insert(QStringLiteral("files"), hash_map);
		write_ok = writeJsonAtomically(QDir(stage_path).filePath(QStringLiteral(".metasiberia-install.json")), metadata, error);
	}
	const QString backup_path = QDir(staging_parent).filePath(QUuid::createUuid().toString(QUuid::WithoutBraces) + QStringLiteral(".previous"));
	QVector<QPair<QString, QString>> moved_previous;
	if(write_ok && !previous_versions.isEmpty() && !QDir().mkpath(backup_path))
	{
		error = QStringLiteral("Cannot create an isolated package-update rollback directory.");
		write_ok = false;
	}
	if(write_ok)
	{
		for(const QPair<QString, QString>& old : previous_versions)
		{
			const QString backup_id_path = QDir(backup_path).filePath(info.id);
			if(!QDir().mkpath(backup_id_path))
			{
				error = QStringLiteral("Cannot prepare the package-update rollback directory.");
				write_ok = false;
				break;
			}
			const QString saved_path = QDir(backup_id_path).filePath(old.first);
			if(!QDir().rename(old.second, saved_path))
			{
				error = QStringLiteral("Could not move the previous package version into the rollback directory.");
				write_ok = false;
				break;
			}
			moved_previous.push_back(qMakePair(old.second, saved_path));
		}
	}
	if(write_ok && !QDir().rename(stage_path, final_path))
	{
		error = QStringLiteral("Atomic activation of the staged plugin directory failed.");
		write_ok = false;
	}
	if(!write_ok)
	{
		for(auto it = moved_previous.crbegin(); it != moved_previous.crend(); ++it)
			QDir().rename(it->second, it->first);
		QDir(stage_path).removeRecursively();
		if(QFileInfo::exists(backup_path)) QDir(backup_path).removeRecursively();
		return false;
	}
	if(QFileInfo::exists(backup_path)) QDir(backup_path).removeRecursively();
	info.installed = true;
	info.source = QStringLiteral("Local package");
	info.installPath = final_path;
	info.installedAt = QDateTime::currentDateTimeUtc();
	info.state = QStringLiteral("Installed — disabled");
	installedInfo = info;
	reload();
	return true;
}


bool PluginPackageManager::removePackage(const QString& pluginId, const QString& version, const bool removeSettings, QString& error)
{
	static const QRegularExpression id_pattern(QStringLiteral("^[a-z0-9]+(?:[.-][a-z0-9]+)*(?:\\.[a-z0-9]+(?:-[a-z0-9]+)*)+$"));
	if(!id_pattern.match(pluginId).hasMatch() || !validVersion(version))
	{
		error = QStringLiteral("Plugin id or version is invalid.");
		return false;
	}
	const QString root = QDir::fromNativeSeparators(QFileInfo(root_path).canonicalFilePath());
	const QFileInfo root_info(root_path);
	if(!root_info.isDir() || root_info.isSymLink())
	{
		error = QStringLiteral("The plugin directory is missing or is a symbolic link.");
		return false;
	}
	const QString target_path = QDir(root_path).filePath(pluginId + QLatin1Char('/') + version);
	const QString target = QDir::fromNativeSeparators(QFileInfo(target_path).canonicalFilePath());
	if(root.isEmpty() || target.isEmpty() || !target.startsWith(root + QLatin1Char('/'), Qt::CaseInsensitive))
	{
		error = QStringLiteral("Plugin removal path escaped the user plugin directory.");
		return false;
	}
	const QFileInfo target_info(target);
	const QFileInfo id_info(QDir(root).filePath(pluginId));
	if(!target_info.isDir() || target_info.isSymLink() || !id_info.isDir() || id_info.isSymLink())
	{
		error = QStringLiteral("Installed plugin directory is missing or is a symbolic link.");
		return false;
	}
	PluginPackageInfo verified;
	if(!PluginPackageValidator::readInstalledDirectory(target, verified, error)) return false;
	if(verified.id != pluginId || verified.version != version)
	{
		error = QStringLiteral("The selected directory does not match the installed package identity.");
		return false;
	}
	if(!QDir(target).removeRecursively())
	{
		error = QStringLiteral("Could not remove the verified plugin directory.");
		return false;
	}
	const QString id_directory = QDir(root).filePath(pluginId);
	if(QDir(id_directory).exists() && QDir(id_directory).entryList(QDir::NoDotAndDotDot | QDir::AllEntries).isEmpty())
		QDir().rmdir(id_directory);
	if(removeSettings)
	{
		const QString settings = settingsDirectoryForId(pluginId);
		const QString settings_parent = QDir(settings_root).filePath(QStringLiteral("plugin-settings"));
		const QFileInfo settings_root_info(settings_root);
		const QFileInfo settings_parent_info(settings_parent);
		const QFileInfo settings_info(settings);
		if(!settings.isEmpty() && settings_info.exists())
		{
			const QString settings_parent_canonical = QDir::fromNativeSeparators(settings_parent_info.canonicalFilePath());
			const QString settings_canonical = QDir::fromNativeSeparators(settings_info.canonicalFilePath());
			if(!settings_root_info.isDir() || settings_root_info.isSymLink() || !settings_parent_info.isDir() || settings_parent_info.isSymLink() ||
				!settings_info.isDir() || settings_info.isSymLink() || settings_parent_canonical.isEmpty() || settings_canonical.isEmpty() ||
				!settings_canonical.startsWith(settings_parent_canonical + QLatin1Char('/'), Qt::CaseInsensitive) ||
				!QDir(settings).removeRecursively())
			{
				error = QStringLiteral("The plugin was removed, but its separate settings directory could not be safely removed.");
				reload();
				return false;
			}
		}
	}
	reload();
	return true;
}


void PluginPackageManager::reload()
{
	installed.clear();
	const QFileInfo root_info(root_path);
	if(!root_info.exists() || !root_info.isDir() || root_info.isSymLink()) return;
	QDir root(root_path);
	const QStringList ids = root.entryList(QDir::Dirs | QDir::NoDotAndDotDot, QDir::Name);
	for(const QString& id : ids)
	{
		if(id.startsWith(QLatin1Char('.'))) continue;
		const QString id_path = root.filePath(id);
		if(QFileInfo(id_path).isSymLink()) continue;
		const QStringList versions = QDir(id_path).entryList(QDir::Dirs | QDir::NoDotAndDotDot, QDir::Name);
		for(const QString& version : versions)
		{
			if(QFileInfo(QDir(id_path).filePath(version)).isSymLink()) continue;
			PluginPackageInfo info;
			QString error;
			if(PluginPackageValidator::readInstalledDirectory(QDir(id_path).filePath(version), info, error, load_icons))
				installed.push_back(info);
		}
	}
	std::sort(installed.begin(), installed.end(), [](const PluginPackageInfo& a, const PluginPackageInfo& b) {
		return a.name.compare(b.name, Qt::CaseInsensitive) < 0;
	});
}
