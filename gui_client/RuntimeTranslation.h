#pragma once

#include <QtCore/QTranslator>
#include <QtCore/QHash>
#include <QtCore/QString>


namespace RuntimeTranslation
{
	enum class UILanguage
	{
		English,
		Russian
	};

	class RuntimeTranslator final : public QTranslator
	{
	public:
		explicit RuntimeTranslator(QObject* parent = nullptr);

		virtual QString translate(const char* context, const char* sourceText, const char* disambiguation = nullptr, int n = -1) const override;

	private:
		static QString key(const char* context, const char* sourceText);

		QHash<QString, QString> ru_translations;
	};
}

