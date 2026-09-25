/*=====================================================================
MainPageHandlers.cpp
--------------------
Copyright Glare Technologies Limited 2021 -
=====================================================================*/
#include "MainPageHandlers.h"


#include "RequestInfo.h"
#include "Response.h"
#include "WebsiteExcep.h"
#include "Escaping.h"
#include "ResponseUtils.h"
#include "WebServerResponseUtils.h"
#include "LoginHandlers.h"
#include "../shared/Version.h"
#include "../server/ServerWorldState.h"
#include <ConPrint.h>
#include <Exception.h>
#include <Lock.h>
#include <StringUtils.h>
#include <PlatformUtils.h>
#include <WebDataStore.h>
#include <webserver/ResponseUtils.h>


namespace MainPageHandlers
{


static std::string parseWorldNameFromSubURL(const std::string& sub_url)
{
	if(!hasPrefix(sub_url, "sub://"))
		return sub_url;

	const size_t host_start = 6; // after "sub://"
	const size_t slash_pos = sub_url.find('/', host_start);
	if(slash_pos == std::string::npos)
		return ""; // URL points to main world.

	const size_t world_start = slash_pos + 1;
	const size_t query_pos = sub_url.find('?', world_start);
	const std::string world_part = (query_pos == std::string::npos) ? sub_url.substr(world_start) : sub_url.substr(world_start, query_pos - world_start);
	return web::Escaping::URLUnescape(world_part);
}


static std::string normaliseWorldNameField(const std::string& world_field)
{
	const std::string trimmed = stripHeadAndTailWhitespace(world_field);
	if(hasPrefix(trimmed, "sub://"))
		return parseWorldNameFromSubURL(trimmed);
	return trimmed;
}


struct AuctionIDLessThan
{
	bool operator() (const ParcelAuction* a, const ParcelAuction* b)
	{
		return a->id < b->id;
	}
};

void renderRootPage(ServerAllWorldsState& world_state, WebDataStore& data_store, const web::RequestInfo& request_info, web::ReplyInfo& reply_info)
{
	//std::string page_out = WebServerResponseUtils::standardHeader(world_state, request_info, /*page title=*/"Substrata");
	//const bool logged_in = LoginHandlers::isLoggedInAsNick(data_store, request_info);

	const std::string root_page_title = "Metasiberia - Metaverse from Siberia";
	const std::string root_page_og_title = "METASIBERIA°";
	const std::string root_page_description = "Metaverse from Siberia · Craft virtually - Inspire reality";
	const std::string root_page_url = "https://vr.metasiberia.com/";
	const std::string root_page_image_url = "https://vr.metasiberia.com/files/main.png";

	const std::string root_page_meta_tags =
		"\t\t<link rel=\"canonical\" href=\"" + root_page_url + "\" />\n"
		"\t\t<meta name=\"description\" content=\"" + web::Escaping::HTMLEscape(root_page_description) + "\" />\n"
		"\t\t<meta name=\"robots\" content=\"index,follow,max-image-preview:large,max-snippet:-1,max-video-preview:-1\" />\n"
		"\t\t<meta property=\"og:site_name\" content=\"Metasiberia\" />\n"
		"\t\t<meta property=\"og:type\" content=\"website\" />\n"
		"\t\t<meta property=\"og:title\" content=\"" + web::Escaping::HTMLEscape(root_page_og_title) + "\" />\n"
		"\t\t<meta property=\"og:description\" content=\"" + web::Escaping::HTMLEscape(root_page_description) + "\" />\n"
		"\t\t<meta property=\"og:url\" content=\"" + root_page_url + "\" />\n"
		"\t\t<meta property=\"og:image\" content=\"" + root_page_image_url + "\" />\n"
		"\t\t<meta property=\"og:image:secure_url\" content=\"" + root_page_image_url + "\" />\n"
		"\t\t<meta property=\"og:image:type\" content=\"image/png\" />\n"
		"\t\t<meta property=\"og:image:width\" content=\"1731\" />\n"
		"\t\t<meta property=\"og:image:height\" content=\"909\" />\n"
		"\t\t<meta property=\"og:image:alt\" content=\"Metasiberia - Metaverse from Siberia\" />\n"
		"\t\t<meta name=\"twitter:card\" content=\"summary_large_image\" />\n"
		"\t\t<meta name=\"twitter:title\" content=\"" + web::Escaping::HTMLEscape(root_page_og_title) + "\" />\n"
		"\t\t<meta name=\"twitter:description\" content=\"" + web::Escaping::HTMLEscape(root_page_description) + "\" />\n"
		"\t\t<meta name=\"twitter:image\" content=\"" + root_page_image_url + "\" />\n"
		"\t\t<script type=\"application/ld+json\">{\"@context\":\"https://schema.org\",\"@type\":\"WebSite\",\"name\":\"Metasiberia\",\"url\":\"https://vr.metasiberia.com/\",\"description\":\"Metaverse from Siberia · Craft virtually - Inspire reality\",\"image\":\"https://vr.metasiberia.com/files/main.png\"}</script>\n";

	std::string page_out = WebServerResponseUtils::standardHTMLHeader(data_store, request_info, /*page title=*/root_page_title, /*extra header tags=*/root_page_meta_tags);
	page_out +=
		"	<body class=\"root-body\">\n"
		"	<div id=\"login\">\n"; // Start login div

	web::UnsafeString logged_in_username;
	bool is_user_admin;
	const bool logged_in = LoginHandlers::isLoggedIn(world_state, request_info, logged_in_username, is_user_admin);

	if(logged_in)
	{
		page_out += "You are logged in as <a href=\"/account\" target=\"_blank\" rel=\"noopener noreferrer\">" + logged_in_username.HTMLEscaped() + "</a>";

		// Add logout button
		page_out += "<form action=\"/logout_post\" method=\"post\">\n";
		page_out += "<input class=\"link-button\" type=\"submit\" value=\"Log out\">\n";
		page_out += "</form>\n";
	}
	else
	{
		page_out += "<a href=\"/login\" target=\"_blank\" rel=\"noopener noreferrer\">log in</a> <br/>\n";
	}
	page_out += 
		"	</div>																									\n"; // End login div


	//page_out += "<img src=\"/files/logo_main_page.png\" alt=\"substrata logo\" class=\"logo-root-page\" />";


	std::string auction_html, latest_news_html, events_html, photos_html;
	{ // lock scope
		WorldStateLock lock(world_state.mutex);

		ServerWorldState* root_world = world_state.getRootWorldState().ptr();

		const TimeStamp now = TimeStamp::currentTime();

		// Collect list of all current auctions
		SmallVector<const ParcelAuction*, 16> current_auctions;
		const ServerWorldState::ParcelMapType& parcels = root_world->getParcels(lock);
		for(auto it = parcels.begin(); it != parcels.end(); ++it)
		{
			const Parcel* parcel = it->second.ptr();
			if(!parcel->parcel_auction_ids.empty())
			{
				const uint32 auction_id = parcel->parcel_auction_ids.back(); // Get most recent auction
				auto res = world_state.parcel_auctions.find(auction_id);
				if(res != world_state.parcel_auctions.end())
				{
					const ParcelAuction* auction = res->second.ptr();
					if(auction->currentlyForSale(now)) // If auction is valid and running:
						current_auctions.push_back(auction);
				}
			}
		}

		// Sort auctions by ID, smallest (= oldest) first.
		std::sort(current_auctions.begin(), current_auctions.end(), AuctionIDLessThan());

		// Generate HTML for auctions
		const int MAX_NUM_AUCTIONS_TO_SHOW = 4;
		auction_html += "<div class=\"root-auction-list-container\">\n";
		int num_auctions_shown = 0;
		for(size_t i=0; (i<current_auctions.size()) && (num_auctions_shown < MAX_NUM_AUCTIONS_TO_SHOW); ++i)
		{
			const ParcelAuction* auction = current_auctions[i];

			if(!auction->screenshot_ids.empty())
			{
				const uint64 shot_id = auction->screenshot_ids[0]; // Get id of close-in screenshot

				const double cur_price_EUR = auction->computeCurrentAuctionPrice();
				const double cur_price_BTC = cur_price_EUR * world_state.BTC_per_EUR;
				const double cur_price_ETH = cur_price_EUR * world_state.ETH_per_EUR;

				auction_html += "<div class=\"root-auction-div\"><a href=\"/parcel_auction/" + toString(auction->id) + "\"><img src=\"/screenshot/" + toString(shot_id) + "\" class=\"root-auction-thumbnail\" alt=\"screenshot\" /></a>  <br/>"
					"&euro;" + doubleToStringNDecimalPlaces(cur_price_EUR, 2) + " / " + doubleToStringNSigFigs(cur_price_BTC, 2) + "&nbsp;BTC / " + doubleToStringNSigFigs(cur_price_ETH, 2) + "&nbsp;ETH</div>";
			}

			num_auctions_shown++;
		}
		auction_html += "</div>\n";

		// If no auctions on substrata site were shown, show OpenSea auctions, if any.
		int opensea_num_shown = 0;
		if(num_auctions_shown == 0)
		{
			auction_html += "<div class=\"root-auction-list-container\">\n";
			for(auto it = world_state.opensea_parcel_listings.begin(); (it != world_state.opensea_parcel_listings.end()) && (opensea_num_shown < 3); ++it)
			{
				const OpenSeaParcelListing& listing = *it;

				auto parcel_res = root_world->getParcels(lock).find(listing.parcel_id); // Look up parcel
				if(parcel_res != root_world->getParcels(lock).end())
				{
					const Parcel* parcel = parcel_res->second.ptr();

					if(parcel->screenshot_ids.size() >= 1)
					{
						const uint64 shot_id = parcel->screenshot_ids[0]; // Close-in screenshot

						const std::string opensea_url = "https://opensea.io/assets/ethereum/0xa4535f84e8d746462f9774319e75b25bc151ba1d/" + listing.parcel_id.toString();

						auction_html += "<div class=\"root-auction-div\"><a href=\"/parcel/" + parcel->id.toString() + "\"><img src=\"/screenshot/" + toString(shot_id) + "\" class=\"root-auction-thumbnail\" alt=\"screenshot\" /></a>  <br/>"
							"<a href=\"/parcel/" + parcel->id.toString() + "\">Parcel " + parcel->id.toString() + "</a> <a href=\"" + opensea_url + "\">View&nbsp;on&nbsp;OpenSea</a></div>";
					}

					opensea_num_shown++;
				}
			}
			auction_html += "</div>\n";
		}

		if(num_auctions_shown == 0 && opensea_num_shown == 0)
			auction_html += "<p>Sorry, there are no parcels for sale here right now.  Please check back later!</p>";



		// Build latest news HTML
		latest_news_html += "<div class=\"root-news-div-container\">\n";		const int max_num_to_display = 4;
		int num_displayed = 0;
		for(auto it = world_state.news_posts.rbegin(); it != world_state.news_posts.rend() && num_displayed < max_num_to_display; ++it)
		{
			NewsPost* post = it->second.ptr();

			if(post->state == NewsPost::State_published)
			{
				latest_news_html += "<div class=\"root-news-div\">";

				const std::string post_url = "/news_post/" + toString(post->id);

				if(post->thumbnail_URL.empty())
					latest_news_html += "<div class=\"root-news-thumb-div\"><a href=\"" + post_url + "\"><img src=\"/files/default_thumb.jpg\" class=\"root-news-thumbnail\" /></a></div>";
				else
					latest_news_html += "<div class=\"root-news-thumb-div\"><a href=\"" + post_url + "\"><img src=\"" + post->thumbnail_URL + "\" class=\"root-news-thumbnail\" /></a></div>";

				latest_news_html += "<div class=\"root-news-title\"><a href=\"" + post_url + "\">" + post->title + "</a></div>";
				//latest_news_html += "<div class=\"root-news-content\"><a href=\"" + post_url + "\">" + web::ResponseUtils::getPrefixWithStrippedTags(post->content, /*max len=*/200) + "</a></div>";

				latest_news_html += "</div>";

				num_displayed++;
			}
		}
		latest_news_html += "</div>\n";


		// Build events HTML
		events_html += "<div class=\"root-events-div-container\">\n";		const int max_num_events_to_display = 4;
		int num_events_displayed = 0;
		for(auto it = world_state.events.rbegin(); (it != world_state.events.rend()) && (num_events_displayed < max_num_events_to_display); ++it)
		{
			const SubEvent* event = it->second.ptr();

			// We don't want to show old events, so end time has to be in the future, or sometime today, e.g. end_time >= (current time - 24 hours)
			const TimeStamp min_end_time(TimeStamp::currentTime().time - 24 * 3600);
			if((event->end_time >= min_end_time) && (event->state == SubEvent::State_published))
			{
				events_html += "<div class=\"root-event-div\">";

				events_html += "<div class=\"root-event-title\"><a href=\"/event/" + toString(event->id) + "\">" + web::Escaping::HTMLEscape(event->title) + "</a></div>";

				events_html += "<div class=\"root-event-description\">";
				const size_t MAX_DESCRIP_SHOW_LEN = 80;
				events_html += web::Escaping::HTMLEscape(event->description.substr(0, MAX_DESCRIP_SHOW_LEN));
				if(event->description.size() > MAX_DESCRIP_SHOW_LEN)
					events_html += "...";
				events_html += "</div>";

				events_html += "<div class=\"root-event-time\">" + event->start_time.dayAndTimeStringUTC() + "</div>";

				events_html += "</div>";

				num_events_displayed++;
			}
		}
		if(num_events_displayed == 0)
			events_html += "There are no upcoming events.  Create one!";
		events_html += "</div>\n";


		//------------------------------- Build root-page photo filmstrip HTML --------------------------
		photos_html.reserve(8192);
		const int max_num_photos_to_display = 36;
		int num_photos_displayed = 0;
		for(auto it = world_state.photos.rbegin(); (it != world_state.photos.rend()) && (num_photos_displayed < max_num_photos_to_display); ++it)
		{
			const Photo* photo = it->second.ptr();
			if(photo->state == Photo::State_published)
			{
				if(num_photos_displayed == 0)
					photos_html += "<div class=\"msb-root-photo-filmstrip-wrap\"><div class=\"msb-root-photo-filmstrip\" aria-label=\"Metasiberia photo gallery\">";

				const std::string escaped_caption = web::Escaping::HTMLEscape(photo->caption);
				photos_html += "<a class=\"msb-root-photo-filmstrip-item\" href=\"/photo/";
				photos_html += toString(photo->id);
				photos_html += "\" title=\"";
				photos_html += escaped_caption;
				photos_html += "\"><img src=\"/photo_thumb_image/";
				photos_html += toString(photo->id);
				photos_html += "\" class=\"msb-root-photo-filmstrip-img\" alt=\"Metasiberia photo\" loading=\"lazy\"/></a>";

				num_photos_displayed++;
			}
		}

		if(num_photos_displayed > 0)
			photos_html += "</div></div>\n";

	} // end lock scope


	Reference<WebDataStoreFile> store_file = data_store.getFragmentFile("root_page.htmlfrag");
	if(store_file.nonNull())
	{
		page_out += std::string(store_file->uncompressed_data.begin(), store_file->uncompressed_data.end());
	}

	StringUtils::replaceFirstInPlace(page_out, "LATEST_NEWS_HTML", latest_news_html);

	StringUtils::replaceFirstInPlace(page_out, "LAND_PARCELS_FOR_SALE_HTML", auction_html);

	StringUtils::replaceFirstInPlace(page_out, "EVENTS_HTML", events_html);

	page_out += "<script src=\"/files/root-page.js\"></script>";
	
	page_out += WebServerResponseUtils::standardFooter(request_info, true, photos_html);

	web::ResponseUtils::writeHTTPOKHeaderAndData(reply_info, page_out);
}


void renderTermsOfUse(ServerAllWorldsState& world_state, const web::RequestInfo& request_info, web::ReplyInfo& reply_info)
{
	std::string page = WebServerResponseUtils::standardHeader(world_state, request_info, /*page title=*/"Условия использования");

	page += "<h1>Metasiberia</h1>";
	page += "<h2>Условия обслуживания</h2>";
	page += "<p>Эти условия обслуживания применяются к веб-сайту metasiberia (по адресу vr.metasiberia.com) и виртуальному миру Metasiberia, который размещен на серверах Reg.ru и доступен через клиентское программное обеспечение.</p>";
	page += "<p>Они вместе составляют «Сервис».</p>";

	page += "<h2>Общие условия</h2>";
	page += "<p>Получая доступ или используя «Сервис», вы соглашаетесь соблюдать эти Условия.</p>";
	page += "<p>Если вы не согласны с какой-либо частью условий, вы не можете получить доступ к Сервису.</p>";
	page += "<p>Не допускается порнография и насилие.</p>";
	page += "<p>Содержимое парселя не должно серьезно и неблагоприятно влиять на производительность или функционирование сервера(ов) Metasiberia или клиента. (Например, не загружайте модели с чрезмерным количеством полигонов или разрешением текстур)</p>";
	page += "<p>Не пытайтесь намеренно вывести из строя или ухудшить работу сервера или клиентов других пользователей.</p>";
	page += "<p>Мы оставляем за собой право отказать в обслуживании любому человеку в любое время и по любой причине.</p>";
	page += "<p>Мы оставляем за собой право изменять условия обслуживания.</p>";


	page += WebServerResponseUtils::standardFooter(request_info, true);

	web::ResponseUtils::writeHTTPOKHeaderAndData(reply_info, page);
}


void renderAboutParcelSales(ServerAllWorldsState& world_state, const web::RequestInfo& request_info, web::ReplyInfo& reply_info)
{
	std::string page = WebServerResponseUtils::standardHeader(world_state, request_info, /*page title=*/"Historical Substrata parcel-sales guide");

	page += "<h2>Dutch Auctions</h2>";

	page += "<p>Parcel sales in Substrata are currently done with a <a href=\"https://en.wikipedia.org/wiki/Dutch_auction\">Dutch (reverse) auction</a>.  A Dutch auction starts from a high price, with the price decreasing over time.</p>";

	page += "<p>The auction stops as soon as someone buys the parcel.  If no one buys the parcel before it reaches the low/reserve price, then the auction stops without a sale.</p>";

	page += "<p>The reason for using a reverse auction is that it avoids the problem of people faking bids, e.g. promising to pay, and then not paying.</p>";
		
	page += "<h2>Payments and Currencies</h2>";

	page += "<h3>PayPal</h3>";

	page += "<p>We accept credit card payments of normal (&lsquo;fiat&rsquo;) money, via <a href=\"https://www.paypal.com/\">PayPal</a>.  This option is perfect for people without cryptocurrency or who don't want to use cryptocurrency.</p>";

	page += "<p>Prices on substrata.info are shown in Euros (EUR / &euro;), but you can pay with your local currency (e.g. USD).  PayPal will convert the payment amount from EUR to your local currency and show it on the PayPal payment page.</p>";

	page += "<h3>Coinbase</h3>";

	page += "<p>We also accept cryptocurrencies via <a href=\"https://www.coinbase.com/\">Coinbase</a>.  We accept all cryptocurrencies that Coinbase accepts, which includes Bitcoin, Ethereum and others.</p>";

	page += "<p>Pricing of BTC and ETH shown on substrata.info is based on the current EUR-BTC and EUR-ETH exchange rate, as retrieved from Coinbase every 30 seconds.</p>";

	page += "<p>The actual amount of BTC and ETH required to purchase a parcel might differ slightly from the amount shown on substrata.info, due to rounding the amount displayed and exchange-rate fluctuations.</p>";

	page += "<h2>Building on your recently purchased Parcel</h2>";

	page += "<p>Did you just win a parcel auction? Congratulations!  Please restart your Substrata client, so that ownership changes of your Parcel are picked up.</p>";

	page += "<p>To view your parcel, click the 'Show parcels' toolbar button in the Substrata client, then double-click on your parcel.  The parcel should show you as the owner in the object editor.";
	page += " If the owner still says 'MrAdmin', then the ownership change has not gone through yet.</p>";

	page += "<h2>Reselling Parcels and NFTs</h2>";

	page += "<p>You can mint a substrata parcel you own as an Ethereum NFT.  This will allow you to sell it or otherwise transfer it to another person.</p>";

	page += "<p>See the <a href=\"/faq\">FAQ</a> for more details.</p>";


	page += "<br/><br/>";
	page += "<a href=\"/\">&lt; Home</a>";

	page += WebServerResponseUtils::standardFooter(request_info, true);

	web::ResponseUtils::writeHTTPOKHeaderAndData(reply_info, page);
}


void renderFAQ(ServerAllWorldsState& world_state, const web::RequestInfo& request_info, web::ReplyInfo& reply_info)
{
 std::string page = WebServerResponseUtils::standardHeader(world_state, request_info, "FAQ — Metasiberia", "", "Questions and Answers about Metasiberia");
 auto loc = [](const std::string& en, const std::string& ru) { return std::string("<span data-msb-en=\"") + web::Escaping::HTMLEscape(en) + "\" data-msb-ru=\"" + web::Escaping::HTMLEscape(ru) + "\">" + ru + "</span>"; };
 auto beginSection = [&](const std::string& en, const std::string& ru) { page += "<section><h2>" + loc(en, ru) + "</h2>"; };
 int question_number = 0;
 auto addQuestion = [&](const std::string& en_q, const std::string& ru_q, const std::string& en_a, const std::string& ru_a) { const std::string number = toString(++question_number) + ". "; page += "<div class=\"faq-item\"><h3>" + number + loc(en_q, ru_q) + "</h3><div class=\"faq-answer\">" + loc(en_a, ru_a) + "</div></div>"; };
 auto endSection = [&]() { page += "</section>"; };

 beginSection("About Metasiberia", "О Metasiberia");
 addQuestion("What is Metasiberia?", "Что такое Metasiberia?",
  R"FAQ(<p>Metasiberia (Метасибирь) is a software platform for creating, hosting, and exploring interactive 3D spaces and digital objects.</p><p>The platform brings together virtual territories, 3D models, voxel objects, images, video, avatars, interactive objects, and programmable elements within a shared networked spatial environment.</p><p>Metasiberia is being developed as an environment in which you can not only explore ready-made virtual spaces but also create your own spatial content.</p>)FAQ",
  R"FAQ(<p>Metasiberia (Метасибирь) — программная платформа для создания, размещения и исследования интерактивных 3D-пространств и цифровых объектов.</p><p>Платформа объединяет виртуальные территории, 3D-модели, воксельные объекты, изображения, видео, аватары, интерактивные объекты и программируемые элементы в общей сетевой пространственной среде.</p><p>Metasiberia развивается как среда, в которой можно не только исследовать готовые виртуальные пространства, но и создавать собственный пространственный контент.</p>)FAQ");
 endSection();

 beginSection("Metasiberia and the Metaverse", "Metasiberia и метавселенная");
 addQuestion("Why can Metasiberia be considered a metaverse?", "Почему Metasiberia можно рассматривать как метавселенную?",
  R"FAQ(<p>The term “metaverse” is not the name of any single software product or the sole technical standard.</p><p>Modern work on an open metaverse involves the tasks of uniting interactive spatial environments, 3D content, users, devices, and various technologies on the basis of compatible approaches and standards.</p><p>The Metaverse Standards Forum regards interoperability as one of the fundamental elements of an open metaverse and coordinates requirements, use cases, test projects, and recommendations for the development of the corresponding standards.</p><p>Source:</p><a href="https://metaverse-standards.org/">https://metaverse-standards.org/</a><p>How this is implemented in Metasiberia:</p><p>the platform is a persistent networked spatial environment with multiple territories, users, objects, avatars, and the ability to create and edit spatial content.</p>)FAQ",
  R"FAQ(<p>Термин «метавселенная» не является названием одного конкретного программного продукта или единственного технического стандарта.</p><p>Современная работа над открытой метавселенной включает задачи объединения интерактивных пространственных сред, 3D-контента, пользователей, устройств и различных технологий на основе совместимых подходов и стандартов.</p><p>Metaverse Standards Forum рассматривает интероперабельность как один из фундаментальных элементов открытой метавселенной и занимается координацией требований, use cases, тестовых проектов и рекомендаций для развития соответствующих стандартов.</p><p>Источник:</p><a href="https://metaverse-standards.org/">https://metaverse-standards.org/</a><p>Как это реализуется в Metasiberia:</p><p>платформа представляет собой постоянную сетевую пространственную среду с несколькими территориями, пользователями, объектами, аватарами и возможностью создания и редактирования пространственного контента.</p>)FAQ");
 endSection();

 beginSection("The Open Metaverse", "Открытая метавселенная");
 addQuestion("What is an open metaverse?", "Что такое открытая метавселенная?",
  R"FAQ(<p>In the context of modern work on the 3D Web, an open metaverse assumes the ability to link different spatial environments, use open standards, and enable interaction between different technologies and systems.</p><p>In the Web of Worlds concept being developed within the Metaverse Standards Forum, virtual worlds are treated as addressable and interconnected spatial environments.</p><p>Source:</p><a href="https://metaverse-standards.org/news/blog/announcing-the-web-of-worlds-whitepaper-a-concrete-path-to-the-open-metaverse/">https://metaverse-standards.org/news/blog/announcing-the-web-of-worlds-whitepaper-a-concrete-path-to-the-open-metaverse/</a>)FAQ",
  R"FAQ(<p>В контексте современных работ по 3D Web открытая метавселенная предполагает возможность связывать различные пространственные среды, использовать открытые стандарты и обеспечивать взаимодействие между различными технологиями и системами.</p><p>В концепции Web of Worlds, разрабатываемой в рамках Metaverse Standards Forum, виртуальные миры рассматриваются как адресуемые и связанные пространственные среды.</p><p>Источник:</p><a href="https://metaverse-standards.org/news/blog/announcing-the-web-of-worlds-whitepaper-a-concrete-path-to-the-open-metaverse/">https://metaverse-standards.org/news/blog/announcing-the-web-of-worlds-whitepaper-a-concrete-path-to-the-open-metaverse/</a>)FAQ");
 endSection();

 beginSection("Interoperability", "Интероперабельность");
 addQuestion("What is interoperability?", "Что такое интероперабельность?",
  R"FAQ(<p>Interoperability is the ability of different systems and components to interact with one another and to use compatible data and mechanisms.</p><p>For the spatial Web this can apply to:</p><ul><li>3D models;</li><li>materials and textures;</li><li>spatial data;</li><li>virtual worlds;</li><li>user identifiers;</li><li>devices;</li><li>client and server systems.</li></ul><p>The Metaverse Standards Forum regards interoperability as a key condition for the development of an open metaverse.</p><p>Source:</p><a href="https://metaverse-standards.org/">https://metaverse-standards.org/</a>)FAQ",
  R"FAQ(<p>Интероперабельность — способность различных систем и компонентов взаимодействовать между собой и использовать совместимые данные и механизмы.</p><p>Для пространственного Web это может относиться к:</p><ul><li>3D-моделям;</li><li>материалам и текстурам;</li><li>пространственным данным;</li><li>виртуальным мирам;</li><li>пользовательским идентификаторам;</li><li>устройствам;</li><li>клиентским и серверным системам.</li></ul><p>Metaverse Standards Forum рассматривает интероперабельность как ключевое условие развития открытой метавселенной.</p><p>Источник:</p><a href="https://metaverse-standards.org/">https://metaverse-standards.org/</a>)FAQ");
 endSection();

 beginSection("Digital Twins", "Цифровые двойники");
 addQuestion("What is a digital twin?", "Что такое цифровой двойник?",
  R"FAQ(<p>A digital twin is a digital representation of a physical object, space, system, or process that is linked to the corresponding real-world object and is intended for its representation, analysis, simulation, monitoring, or interaction.</p><p>It is important to distinguish a digital twin from an ordinary 3D model: a geometric model by itself does not make an object a digital twin.</p><p>Source:</p><a href="https://metaverse-standards.org/domain-groups/real-virtual-world-integration/">https://metaverse-standards.org/domain-groups/real-virtual-world-integration/</a>)FAQ",
  R"FAQ(<p>Цифровой двойник — цифровое представление физического объекта, пространства, системы или процесса, связанное с соответствующим объектом реального мира и предназначенное для его представления, анализа, моделирования, мониторинга или взаимодействия.</p><p>Важно отличать цифровой двойник от обычной 3D-модели: сама по себе геометрическая модель ещё не делает объект цифровым двойником.</p><p>Источник:</p><a href="https://metaverse-standards.org/domain-groups/real-virtual-world-integration/">https://metaverse-standards.org/domain-groups/real-virtual-world-integration/</a>)FAQ");
 endSection();

 beginSection("Technology and Glare Core", "Технологии и Glare Core");
 addQuestion("What technology is Metasiberia based on?", "На какой технологии основана Metasiberia?",
  R"FAQ(<p>Metasiberia uses Glare Core as its technological foundation and extends it with its own components for virtual worlds and spatial interaction.</p><p>Glare Core is a set of reusable C++ components developed by Glare Technologies and used in its software projects.</p><p>Glare Core contains components for:</p><ul><li>computer graphics;</li><li>OpenGL;</li><li>mathematics;</li><li>networking;</li><li>physics;</li><li>video;</li><li>various general-purpose systems.</li></ul><p>On top of the Glare Core components, Metasiberia implements its own subsystems for virtual worlds, user territories, avatars, voxels, Gaussian Splatting, WebClient, user content, and editing tools.</p><p>Source:</p><a href="https://github.com/glaretechnologies/glare-core">https://github.com/glaretechnologies/glare-core</a>)FAQ",
  R"FAQ(<p>Metasiberia использует Glare Core в качестве технологической основы и расширяет его собственными компонентами для виртуальных миров и пространственного взаимодействия.</p><p>Glare Core — набор переиспользуемых C++-компонентов, разработанных Glare Technologies и используемых в её программных проектах.</p><p>В Glare Core присутствуют компоненты для:</p><ul><li>компьютерной графики;</li><li>OpenGL;</li><li>математики;</li><li>сетевого взаимодействия;</li><li>физики;</li><li>видео;</li><li>различных систем общего назначения.</li></ul><p>Поверх компонентов Glare Core в Metasiberia реализованы собственные подсистемы виртуальных миров, пользовательских территорий, аватаров, вокселей, Gaussian Splatting, WebClient, пользовательского контента и инструментов редактирования.</p><p>Источник:</p><a href="https://github.com/glaretechnologies/glare-core">https://github.com/glaretechnologies/glare-core</a>)FAQ");
 addQuestion("Who created Glare Core?", "Кто создал Glare Core?",
  R"FAQ(<p>Glare Core is developed by Glare Technologies.</p><p>Glare Technologies is known, among other things, for the development of Indigo Renderer and Chaotica.</p><p>Source:</p><a href="https://github.com/glaretechnologies/glare-core">https://github.com/glaretechnologies/glare-core</a>)FAQ",
  R"FAQ(<p>Glare Core разрабатывается Glare Technologies.</p><p>Glare Technologies известна, в частности, разработкой Indigo Renderer и Chaotica.</p><p>Источник:</p><a href="https://github.com/glaretechnologies/glare-core">https://github.com/glaretechnologies/glare-core</a>)FAQ");
 endSection();

 beginSection("Supported 3D Formats", "Поддерживаемые 3D-форматы");
 addQuestion("Which 3D models does Metasiberia support?", "Какие 3D-модели поддерживает Metasiberia?",
  R"FAQ(<p>The current implementation supports the following formats and types of 3D content:</p><p>3D models:</p><p>OBJ, glTF, GLB, STL, VOX, IGMESH, BMESH, VRM, SUBVOX.</p><p>3D Gaussian Splatting:</p><p>PLY, SPLAT, SPZ, compressed PLY, KSPLAT, SOG, LCC, LCC2.</p><p>Images:</p><p>JPG/JPEG, PNG, BMP, TGA, EXR, GIF, KTX, KTX2, BASIS and others depending on the platform and build.</p><p>Video:</p><p>MP4 and browser/native video pipeline.</p><p>Voxels:</p><p>VOX, SUBVOX.</p><p>The degree of support varies for individual formats.</p>)FAQ",
  R"FAQ(<p>Текущая реализация поддерживает следующие форматы и типы 3D-контента:</p><p>3D-модели:</p><p>OBJ, glTF, GLB, STL, VOX, IGMESH, BMESH, VRM, SUBVOX.</p><p>3D Gaussian Splatting:</p><p>PLY, SPLAT, SPZ, compressed PLY, KSPLAT, SOG, LCC, LCC2.</p><p>Изображения:</p><p>JPG/JPEG, PNG, BMP, TGA, EXR, GIF, KTX, KTX2, BASIS и другие в зависимости от платформы и сборки.</p><p>Видео:</p><p>MP4 и browser/native video pipeline.</p><p>Воксели:</p><p>VOX, SUBVOX.</p><p>Для отдельных форматов степень поддержки различается.</p>)FAQ");
 addQuestion("What is glTF?", "Что такое glTF?",
  R"FAQ(<p>glTF (GL Transmission Format) is an open format for the transmission of 3D assets developed by the Khronos Group.</p><p>The glTF specification is designed for the efficient transmission and loading of 3D scenes.</p><p>Source:</p><a href="https://registry.khronos.org/glTF/">https://registry.khronos.org/glTF/</a>)FAQ",
  R"FAQ(<p>glTF (GL Transmission Format) — открытый формат передачи 3D-ресурсов, разработанный Khronos Group.</p><p>Спецификация glTF предназначена для эффективной передачи и загрузки 3D-сцен.</p><p>Источник:</p><a href="https://registry.khronos.org/glTF/">https://registry.khronos.org/glTF/</a>)FAQ");
 addQuestion("What are KTX and KTX2?", "Что такое KTX и KTX2?",
  R"FAQ(<p>KTX (Khronos Texture) is an open container format from the Khronos Group for storing and efficiently delivering textures.</p><p>KTX 2.0 is intended for modern graphics applications and supports various texture compression and transmission mechanisms.</p><p>Source:</p><a href="https://registry.khronos.org/KTX/">https://registry.khronos.org/KTX/</a>)FAQ",
  R"FAQ(<p>KTX (Khronos Texture) — открытый контейнерный формат Khronos Group для хранения и эффективной доставки текстур.</p><p>KTX 2.0 предназначен для современных графических приложений и поддерживает различные механизмы сжатия и передачи текстур.</p><p>Источник:</p><a href="https://registry.khronos.org/KTX/">https://registry.khronos.org/KTX/</a>)FAQ");
 endSection();

 beginSection("3D Gaussian Splatting", "3D Gaussian Splatting");
 addQuestion("What is 3D Gaussian Splatting?", "Что такое 3D Gaussian Splatting?",
  R"FAQ(<p>3D Gaussian Splatting (3DGS) is a method of representing and visualizing three-dimensional scenes using a set of three-dimensional Gaussian primitives.</p><p>In the original scientific paper by Kerbl et al., a scene is represented by 3D Gaussians that are optimized with respect to their spatial characteristics and visual appearance.</p><p>Source:</p><a href="https://arxiv.org/abs/2308.04079">https://arxiv.org/abs/2308.04079</a>)FAQ",
  R"FAQ(<p>3D Gaussian Splatting (3DGS) — метод представления и визуализации трёхмерных сцен с использованием набора трёхмерных гауссовых примитивов.</p><p>В оригинальной научной работе Kerbl и соавторов сцена представляется 3D-гауссианами, которые оптимизируются с учётом их пространственных характеристик и визуального представления.</p><p>Источник:</p><a href="https://arxiv.org/abs/2308.04079">https://arxiv.org/abs/2308.04079</a>)FAQ");
 addQuestion("Why is 3DGS interesting for Metasiberia?", "Почему 3DGS интересен для Metasiberia?",
  R"FAQ(<p>3D Gaussian Splatting is especially interesting for spatial scenes obtained by:</p><ul><li>3D scanning;</li><li>photogrammetry;</li><li>capture of real spaces;</li><li>reconstruction of objects and environments.</li></ul><p>In Metasiberia this makes it possible to use modern representations of spatial scenes alongside traditional polygonal models.</p><p>This opens opportunities for working with:</p><ul><li>architecture;</li><li>works of art;</li><li>cultural-heritage objects;</li><li>real interiors;</li><li>urban spaces;</li><li>natural objects;</li><li>results of spatial scanning.</li></ul>)FAQ",
  R"FAQ(<p>3D Gaussian Splatting особенно интересен для пространственных сцен, полученных посредством:</p><ul><li>3D-сканирования;</li><li>фотограмметрии;</li><li>захвата реальных пространств;</li><li>реконструкции объектов и окружающей среды.</li></ul><p>В Metasiberia это позволяет использовать современные представления пространственных сцен наряду с традиционными polygonal-моделями.</p><p>Это открывает возможности для работы с:</p><ul><li>архитектурой;</li><li>произведениями искусства;</li><li>объектами культурного наследия;</li><li>реальными помещениями;</li><li>городскими пространствами;</li><li>природными объектами;</li><li>результатами пространственного сканирования.</li></ul>)FAQ");
 addQuestion("Does Metasiberia support PLY for 3DGS?", "Поддерживает ли Metasiberia PLY для 3DGS?",
  R"FAQ(<p>Yes.</p><p>The current implementation includes a native decoder for binary little-endian 3DGS PLY.</p><p>For the correct native path the file must contain the necessary 3DGS elements and properties, including coordinates x, y, z, opacity, scale, and rotation.</p><p>It is important to distinguish ordinary PLY from a PLY file that contains 3D Gaussian Splatting data.</p>)FAQ",
  R"FAQ(<p>Да.</p><p>В текущей реализации присутствует native decoder для binary little-endian 3DGS PLY.</p><p>Для корректного native-пути файл должен содержать необходимые элементы и свойства 3DGS, включая координаты x, y, z, opacity, scale и rotation.</p><p>Важно отличать обычный PLY от PLY-файла, содержащего данные 3D Gaussian Splatting.</p>)FAQ");
 addQuestion("Which 3DGS formats does Metasiberia support?", "Какие форматы 3DGS поддерживает Metasiberia?", "", "");
 page += "<table><thead><tr><th>" + loc("Format", "Формат") + "</th><th>" + loc("Recognized", "Распознаётся") + "</th><th>" + loc("Native decode", "Native decode") + "</th><th>" + loc("Conversion / fallback", "Conversion / fallback") + "</th></tr></thead><tbody>";
 page += "<tr><td>.ply</td><td>" + loc("Yes", "Да") + "</td><td>" + loc("Yes, for native 3DGS binary little-endian PLY", "Да, для native 3DGS binary little-endian PLY") + "</td><td>" + loc("Not required for the standard native path", "Не требуется для стандартного native пути") + "</td></tr>";
 page += "<tr><td>.compressed.ply</td><td>" + loc("Yes", "Да") + "</td><td>" + loc("No", "Нет") + "</td><td>" + loc("Yes", "Да") + "</td></tr>";
 page += "<tr><td>.splat</td><td>" + loc("Yes", "Да") + "</td><td>" + loc("Yes", "Да") + "</td><td>" + loc("Not required", "Не требуется") + "</td></tr>";
 page += "<tr><td>.spz</td><td>" + loc("Yes", "Да") + "</td><td>" + loc("Yes, for native v4", "Да, для native v4") + "</td><td>" + loc("Conversion may be used for other variants", "Для других вариантов может использоваться conversion") + "</td></tr>";
 page += "<tr><td>.ksplat</td><td>" + loc("Yes", "Да") + "</td><td>" + loc("No", "Нет") + "</td><td>" + loc("Yes", "Да") + "</td></tr>";
 page += "<tr><td>.sog</td><td>" + loc("Yes", "Да") + "</td><td>" + loc("No", "Нет") + "</td><td>" + loc("Yes", "Да") + "</td></tr>";
 page += "<tr><td>.lcc</td><td>" + loc("Yes", "Да") + "</td><td>" + loc("No", "Нет") + "</td><td>" + loc("Yes", "Да") + "</td></tr>";
 page += "<tr><td>.lcc2</td><td>" + loc("Yes", "Да") + "</td><td>" + loc("No", "Нет") + "</td><td>" + loc("Yes", "Да") + "</td></tr>";
 page += "</tbody></table>";
 endSection();

 beginSection("Voxel Objects", "Воксельные объекты");
 addQuestion("What is a voxel?", "Что такое воксель?",
  R"FAQ(<p>A voxel is an element of a volumetric digital representation, analogous to a pixel in a two-dimensional image but belonging to three-dimensional space.</p><p>Metasiberia supports voxel objects as well as their creation and editing directly in the client.</p>)FAQ",
  R"FAQ(<p>Воксель — элемент объёмного цифрового представления, аналогичный пикселю в двумерном изображении, но относящийся к трёхмерному пространству.</p><p>В Metasiberia поддерживаются воксельные объекты, а также их создание и редактирование непосредственно в клиенте.</p>)FAQ");
 addQuestion("How do I create a voxel object?", "Как создать воксельный объект?",
  R"FAQ(<p>On a plot where the user has editing rights:</p><ul><li>select “Add voxels”;</li><li>create the first block;</li><li>while holding Ctrl, use the left mouse button to add new blocks;</li><li>the size of the object can be changed via Scale.</li></ul><p>VOX and SUBVOX are supported.</p>)FAQ",
  R"FAQ(<p>На участке, где пользователь имеет права редактирования:</p><ul><li>выберите «Добавить воксели»;</li><li>создайте первый блок;</li><li>удерживая Ctrl, используйте левую кнопку мыши для добавления новых блоков;</li><li>размер объекта можно изменять через Scale.</li></ul><p>Поддерживаются VOX и SUBVOX.</p>)FAQ");
 endSection();

 beginSection("Parcels and Territories", "Участки и территории");
 addQuestion("What is a parcel?", "Что такое parcel?",
  R"FAQ(<p>A parcel is a designated territory of a virtual world for which ownership and editing rights can be defined.</p><p>In Metasiberia a parcel can be used to create one’s own territory and to place spatial content.</p>)FAQ",
  R"FAQ(<p>Parcel — выделенная территория виртуального мира, для которой могут быть определены права владения и редактирования.</p><p>В Metasiberia parcel может использоваться для создания собственной территории и размещения пространственного контента.</p>)FAQ");
 addQuestion("Can other users be granted editing rights?", "Можно ли предоставить другим пользователям права редактирования?",
  R"FAQ(<p>Yes.</p><p>The system provides the ability to add users as editors of a territory.</p><p>Depending on the settings, other users can be given the ability to create, modify, and delete objects.</p><p>Mechanisms for collaborative editing of a territory are also provided.</p>)FAQ",
  R"FAQ(<p>Да.</p><p>В системе предусмотрена возможность добавления пользователей в качестве редакторов территории.</p><p>В зависимости от настроек можно предоставлять другим пользователям возможность создавать, изменять и удалять объекты.</p><p>Также предусмотрены механизмы общего редактирования территории.</p>)FAQ");
 addQuestion("Can a plot be purchased?", "Можно ли приобрести участок?",
  R"FAQ(<p>At present, plots are acquired by individual request.</p><p>A mechanism for direct purchase through a dedicated store may be added in the future.</p>)FAQ",
  R"FAQ(<p>В настоящее время приобретение участков осуществляется по индивидуальному запросу.</p><p>Механизм прямого приобретения через отдельный магазин может быть добавлен в дальнейшем.</p>)FAQ");
 endSection();

 beginSection("Interactive Objects and Scripting", "Интерактивные объекты и скрипты");
 addQuestion("Can interactive objects be created?", "Можно ли создавать интерактивные объекты?",
  R"FAQ(<p>Yes.</p><p>Metasiberia supports programmable object behavior.</p><p>The system implements object event handlers and a Lua programming environment.</p>)FAQ",
  R"FAQ(<p>Да.</p><p>Metasiberia поддерживает программируемое поведение объектов.</p><p>В системе реализованы обработчики событий объектов и программная среда Lua.</p>)FAQ");
 addQuestion("In which language are objects programmed?", "На каком языке программируются объекты?",
  R"FAQ(<p>Scripts use Lua.</p><p>Scripts can be used to create object behavior and interactive elements.</p><p>The implementation also includes an API for working with certain interactive systems, including particle emitters.</p>)FAQ",
  R"FAQ(<p>Для скриптов используется Lua.</p><p>Скрипты могут использоваться для создания поведения объектов и интерактивных элементов.</p><p>В реализации также присутствует API для работы с некоторыми интерактивными системами, включая particle emitters.</p>)FAQ");
 endSection();

 beginSection("Avatars", "Аватары");
 addQuestion("Can avatars be created?", "Можно ли создавать аватаров?",
  R"FAQ(<p>Yes.</p><p>Metasiberia has an avatar system that includes the avatar model, materials, additional elements, and associated resources.</p>)FAQ",
  R"FAQ(<p>Да.</p><p>Metasiberia имеет систему аватаров, включающую модель аватара, материалы, дополнительные элементы и связанные ресурсы.</p>)FAQ");
 endSection();

 beginSection("WebClient", "WebClient");
 addQuestion("Can Metasiberia be used through a browser?", "Можно ли использовать Metasiberia через браузер?",
  R"FAQ(<p>Yes.</p><p>Metasiberia implements a WebClient that uses Emscripten and WebGL technologies.</p><p>The WebClient allows the spatial client to be used in a browser environment.</p>)FAQ",
  R"FAQ(<p>Да.</p><p>В Metasiberia реализован WebClient, использующий технологии Emscripten и WebGL.</p><p>WebClient позволяет использовать пространственный клиент в браузерной среде.</p>)FAQ");
 endSection();

 beginSection("WebGL", "WebGL");
 addQuestion("Does Metasiberia use WebGL?", "Использует ли Metasiberia WebGL?",
  R"FAQ(<p>Yes.</p><p>The WebClient uses WebGL to display graphics in the browser.</p><p>WebGL is a standardized JavaScript API for rendering interactive 2D and 3D graphics in compatible browsers.</p><p>Source:</p><a href="https://registry.khronos.org/webgl/">https://registry.khronos.org/webgl/</a>)FAQ",
  R"FAQ(<p>Да.</p><p>WebClient использует WebGL для отображения графики в браузере.</p><p>WebGL является стандартизованным JavaScript API для рендеринга интерактивной 2D- и 3D-графики в совместимых браузерах.</p><p>Источник:</p><a href="https://registry.khronos.org/webgl/">https://registry.khronos.org/webgl/</a>)FAQ");
 endSection();

 beginSection("OpenXR", "OpenXR");
 addQuestion("Is OpenXR supported?", "Поддерживается ли OpenXR?",
  R"FAQ(<p>The Metasiberia code contains OpenXR integration that is activated with the appropriate configuration and a compatible XR runtime.</p><p>OpenXR is an open standard from the Khronos Group for applications to interact with XR platforms and devices.</p><p>Source:</p><a href="https://www.khronos.org/openxr/">https://www.khronos.org/openxr/</a>)FAQ",
  R"FAQ(<p>В коде Metasiberia присутствует интеграция OpenXR, которая активируется при соответствующей конфигурации и наличии совместимого XR runtime.</p><p>OpenXR — открытый стандарт Khronos Group для взаимодействия приложений с XR-платформами и устройствами.</p><p>Источник:</p><a href="https://www.khronos.org/openxr/">https://www.khronos.org/openxr/</a>)FAQ");
 endSection();

 beginSection("Performance and Optimisation", "Производительность и оптимизация");
 addQuestion("Why can’t overly heavy models be uploaded?", "Почему нельзя загружать слишком тяжёлые модели?",
  R"FAQ(<p>Metasiberia is a networked spatial environment in which a single user object can potentially affect the performance of other users.</p><p>A large number of:</p><ul><li>polygons;</li><li>textures;</li><li>objects;</li><li>3DGS data;</li><li>animations;</li><li>multimedia resources</li></ul><p>can increase the load on the client, GPU, memory, network, and other system components.</p><p>Therefore it is recommended to optimize spatial content before publication.</p>)FAQ",
  R"FAQ(<p>Metasiberia является сетевой пространственной средой, в которой один пользовательский объект потенциально может влиять на производительность других пользователей.</p><p>Большое количество:</p><ul><li>полигонов;</li><li>текстур;</li><li>объектов;</li><li>данных 3DGS;</li><li>анимаций;</li><li>мультимедийных ресурсов</li></ul><p>может увеличивать нагрузку на клиент, GPU, память, сеть и другие компоненты системы.</p><p>Поэтому перед публикацией рекомендуется оптимизировать пространственный контент.</p>)FAQ");
 endSection();

 beginSection("3D Scanning", "3D-сканирование");
 addQuestion("Can Metasiberia be used for 3D-scanning results?", "Можно ли использовать Metasiberia для результатов 3D-сканирования?",
  R"FAQ(<p>Yes.</p><p>Results of 3D scanning can be used as a source of spatial content if they are converted into a supported format.</p><p>An especially interesting direction is the use of 3D Gaussian Splatting.</p><p>In this way the following can be transferred into virtual space:</p><ul><li>real interiors;</li><li>buildings;</li><li>works of art;</li><li>archaeological objects;</li><li>cultural-heritage objects;</li><li>urban spaces;</li><li>natural objects.</li></ul>)FAQ",
  R"FAQ(<p>Да.</p><p>Результаты 3D-сканирования могут использоваться как источник пространственного контента, если они преобразованы в поддерживаемый формат.</p><p>Особенно интересным направлением является использование 3D Gaussian Splatting.</p><p>Таким способом в виртуальное пространство могут переноситься:</p><ul><li>реальные помещения;</li><li>здания;</li><li>произведения искусства;</li><li>археологические объекты;</li><li>объекты культурного наследия;</li><li>городские пространства;</li><li>природные объекты.</li></ul>)FAQ");
 endSection();

 beginSection("Digital Twins in Metasiberia", "Цифровые двойники в Metasiberia");
 addQuestion("Can digital twins of real objects be created?", "Можно ли создавать цифровые двойники реальных объектов?",
  R"FAQ(<p>Yes.</p><p>Metasiberia can be used to create digital representations of real objects and spaces.</p><p>The degree to which the digital object corresponds to its physical prototype depends on the source data, the scanning or modeling method, and the quality of subsequent processing.</p>)FAQ",
  R"FAQ(<p>Да.</p><p>Metasiberia может использоваться для создания цифровых представлений реальных объектов и пространств.</p><p>При этом степень соответствия цифрового объекта физическому прототипу зависит от исходных данных, метода сканирования или моделирования и качества последующей обработки.</p>)FAQ");
 endSection();

 beginSection("Open Standards", "Открытые стандарты");
 addQuestion("Does Metasiberia use open standards?", "Использует ли Metasiberia открытые стандарты?",
  R"FAQ(<p>Yes.</p><p>Various parts of the technology stack use open technologies and standards, including:</p><ul><li>glTF;</li><li>KTX/KTX2;</li><li>WebGL;</li><li>OpenXR;</li><li>other open formats and technologies.</li></ul><p>The Khronos Group develops open standards in the fields of 3D graphics, XR, and related technologies.</p><p>Source:</p><a href="https://www.khronos.org/">https://www.khronos.org/</a>)FAQ",
  R"FAQ(<p>Да.</p><p>В различных частях технологического стека используются открытые технологии и стандарты, включая:</p><ul><li>glTF;</li><li>KTX/KTX2;</li><li>WebGL;</li><li>OpenXR;</li><li>другие открытые форматы и технологии.</li></ul><p>Khronos Group разрабатывает открытые стандарты в области 3D-графики, XR и смежных технологий.</p><p>Источник:</p><a href="https://www.khronos.org/">https://www.khronos.org/</a>)FAQ");
 endSection();

 beginSection("The 3D Web", "3D Web");
 addQuestion("How is Metasiberia related to the development of the 3D Web?", "Как Metasiberia связана с развитием 3D Web?",
  R"FAQ(<p>Metasiberia uses technologies and architectural approaches that intersect with the tasks considered in modern 3D Web research:</p><ul><li>interactive spatial environments;</li><li>Web access;</li><li>transmission of 3D content;</li><li>interoperability;</li><li>linking of physical and virtual spaces.</li></ul><p>The Metaverse Standards Forum separately investigates the efficient delivery of 3D experiences, interoperability of 3D content, linking of virtual worlds, and interaction between physical and virtual worlds.</p><p>Source:</p><a href="https://metaverse-standards.org/">https://metaverse-standards.org/</a><p>Important:</p><p>this does not mean that Metasiberia is certified by the Metaverse Standards Forum or officially complies with standards established by the Forum.</p>)FAQ",
  R"FAQ(<p>Metasiberia использует технологии и архитектурные подходы, которые пересекаются с задачами, рассматриваемыми в современных исследованиях 3D Web:</p><ul><li>интерактивные пространственные среды;</li><li>Web-доступ;</li><li>передача 3D-контента;</li><li>интероперабельность;</li><li>связь физических и виртуальных пространств.</li></ul><p>Metaverse Standards Forum отдельно исследует эффективную доставку 3D-опыта, интероперабельность 3D-контента, связывание виртуальных миров и взаимодействие физических и виртуальных миров.</p><p>Источник:</p><a href="https://metaverse-standards.org/">https://metaverse-standards.org/</a><p>Важно:</p><p>это НЕ означает, что Metasiberia сертифицирована Metaverse Standards Forum или официально соответствует стандартам, установленным Форумом.</p>)FAQ");
 endSection();

 beginSection("Applications and Examples", "Применения и примеры");
 addQuestion("What can Metasiberia be used for?", "Для чего может использоваться Metasiberia?",
  R"FAQ(<p>Metasiberia can be used to create and host:</p><ul><li>virtual worlds;</li><li>digital twins;</li><li>digital copies of real objects;</li><li>urban spaces;</li><li>architectural projects;</li><li>cultural-heritage objects;</li><li>historical reconstructions;</li><li>educational spaces;</li><li>artistic projects;</li><li>interactive exhibitions;</li><li>scientific visualizations;</li><li>results of 3D scanning;</li><li>user spaces.</li></ul>)FAQ",
  R"FAQ(<p>Metasiberia может использоваться для создания и размещения:</p><ul><li>виртуальных миров;</li><li>цифровых двойников;</li><li>цифровых копий реальных объектов;</li><li>городских пространств;</li><li>архитектурных проектов;</li><li>объектов культурного наследия;</li><li>исторических реконструкций;</li><li>образовательных пространств;</li><li>художественных проектов;</li><li>интерактивных выставок;</li><li>научных визуализаций;</li><li>результатов 3D-сканирования;</li><li>пользовательских пространств.</li></ul>)FAQ");
 addQuestion("Can Metasiberia be used in education?", "Можно ли использовать Metasiberia в образовании?",
  R"FAQ(<p>Yes.</p><p>The spatial environment can be used for:</p><ul><li>virtual excursions;</li><li>studying architecture;</li><li>working with cultural-heritage objects;</li><li>historical reconstructions;</li><li>visualization of scientific data;</li><li>interactive laboratories;</li><li>studying 3D modeling;</li><li>creating one’s own digital spaces.</li></ul>)FAQ",
  R"FAQ(<p>Да.</p><p>Пространственная среда может использоваться для:</p><ul><li>виртуальных экскурсий;</li><li>изучения архитектуры;</li><li>работы с объектами культурного наследия;</li><li>исторических реконструкций;</li><li>визуализации научных данных;</li><li>интерактивных лабораторий;</li><li>изучения 3D-моделирования;</li><li>создания собственных цифровых пространств.</li></ul>)FAQ");
 addQuestion("Can works of art be placed?", "Можно ли размещать произведения искусства?",
  R"FAQ(<p>Yes.</p><p>Works of art can be presented as:</p><ul><li>3D models;</li><li>scanned objects;</li><li>images;</li><li>video;</li><li>3D Gaussian Splatting;</li><li>interactive spatial installations.</li></ul>)FAQ",
  R"FAQ(<p>Да.</p><p>Произведения искусства могут быть представлены в виде:</p><ul><li>3D-моделей;</li><li>отсканированных объектов;</li><li>изображений;</li><li>видео;</li><li>3D Gaussian Splatting;</li><li>интерактивных пространственных инсталляций.</li></ul>)FAQ");
 endSection();

 beginSection("User Content and Responsibility", "Пользовательский контент и ответственность");
 addQuestion("Who is responsible for the posted content?", "Кто отвечает за размещённый контент?",
  R"FAQ(<p>The user is responsible for the content they create and post within the applicable legislation and the Metasiberia Terms of Use.</p><p>The user must possess the necessary rights to use the uploaded models, images, video, scripts, and other materials.</p><p>Link:</p><p><a href="/terms">/terms</a></p>)FAQ",
  R"FAQ(<p>Пользователь отвечает за создаваемый и размещаемый им контент в пределах применимого законодательства и Условий использования Metasiberia.</p><p>Пользователь должен обладать необходимыми правами на использование загружаемых моделей, изображений, видео, текстов и других материалов.</p><p>Ссылка:</p><p><a href="/terms">/terms</a></p>)FAQ");
 addQuestion("What materials are prohibited?", "Какие материалы запрещены?",
  R"FAQ(<p>Restrictions on user content and behavior are set out in the Metasiberia Terms of Use.</p><p>In particular, certain types of sexual and violent content, threats, bullying, harassment, aggressive behavior, political and religious propaganda in the cases provided by the rules, and other behavior incompatible with the principles of good-faith use of the Service are prohibited.</p><p>Detailed rules:</p><p><a href="/terms">/terms</a></p>)FAQ",
  R"FAQ(<p>Ограничения на пользовательский контент и поведение установлены в Условиях использования Metasiberia.</p><p>В частности, запрещаются определённые виды сексуального и насильственного контента, угрозы, травля, преследование, агрессивное поведение, политическая и религиозная пропаганда в предусмотренных правилами случаях и иное поведение, несовместимое с принципами добросовестного использования Сервиса.</p><p>Подробные правила:</p><p><a href="/terms">/terms</a></p>)FAQ");
 endSection();

 beginSection("Scientific Visualisation", "Научная визуализация");
 addQuestion("Can Metasiberia be used for scientific visualization?", "Можно ли использовать Metasiberia для научной визуализации?",
  R"FAQ(<p>Yes.</p><p>Metasiberia can be used for the visualization of spatial data, scientific objects, modeling results, and research.</p><p>Depending on the task, the following may be applied:</p><ul><li>3D models;</li><li>voxels;</li><li>images;</li><li>3D Gaussian Splatting;</li><li>interactive objects;</li><li>programmable elements.</li></ul>)FAQ",
  R"FAQ(<p>Да.</p><p>Metasiberia может использоваться для визуализации пространственных данных, научных объектов, результатов моделирования и исследований.</p><p>В зависимости от задачи могут применяться:</p><ul><li>3D-модели;</li><li>воксели;</li><li>изображения;</li><li>3D Gaussian Splatting;</li><li>интерактивные объекты;</li><li>программируемые элементы.</li></ul>)FAQ");
 endSection();

 beginSection("Cultural Heritage", "Культурное наследие");
 addQuestion("Can Metasiberia be used for cultural heritage?", "Можно ли использовать Metasiberia для культурного наследия?",
  R"FAQ(<p>Yes.</p><p>Metasiberia can be used to present:</p><ul><li>architectural objects;</li><li>historical reconstructions;</li><li>museum objects;</li><li>archaeological finds;</li><li>works of art;</li><li>digital twins;</li><li>results of 3D scanning.</li></ul><p>The accuracy of the digital representation depends on the quality of the source data and the reconstruction method.</p>)FAQ",
  R"FAQ(<p>Да.</p><p>Metasiberia может использоваться для представления:</p><ul><li>архитектурных объектов;</li><li>исторических реконструкций;</li><li>музейных объектов;</li><li>археологических находок;</li><li>произведений искусства;</li><li>цифровых двойников;</li><li>результатов 3D-сканирования.</li></ul><p>При этом достоверность цифрового представления зависит от качества исходных данных и метода реконструкции.</p>)FAQ");
 endSection();

 beginSection("User Virtual Spaces", "Собственные виртуальные пространства");
 addQuestion("Can one’s own virtual spaces be created?", "Можно ли создавать собственные виртуальные пространства?",
  R"FAQ(<p>Yes.</p><p>User territories can be used to create one’s own spaces, place objects, works of art, educational and other spatial content.</p><p>The territory owner can grant editing rights to other users in accordance with the provided mechanisms.</p>)FAQ",
  R"FAQ(<p>Да.</p><p>Пользовательские территории могут использоваться для создания собственных пространств, размещения объектов, произведений искусства, образовательного и другого пространственного контента.</p><p>Владелец территории может предоставлять права редактирования другим пользователям в соответствии с предусмотренными механизмами.</p>)FAQ");
 endSection();

 beginSection("Help and Support", "Помощь по Metasiberia");
 addQuestion("Where can I get help with Metasiberia?", "Где получить помощь по Metasiberia?",
  R"FAQ(<p>Questions about working with Metasiberia can be addressed through the project’s official channels.</p><p>Telegram:</p><a href="https://t.me/metasiberia_metaverse">https://t.me/metasiberia_metaverse</a><p>VK:</p><a href="https://vk.com/metasiberia_official">https://vk.com/metasiberia_official</a>)FAQ",
  R"FAQ(<p>По вопросам работы Metasiberia можно обратиться через официальные каналы проекта.</p><p>Telegram:</p><a href="https://t.me/metasiberia_metaverse">https://t.me/metasiberia_metaverse</a><p>VK:</p><a href="https://vk.com/metasiberia_official">https://vk.com/metasiberia_official</a>)FAQ");
 endSection();

 page += "<p><strong>" + loc("Technical and Scientific Sources", "Технические и научные источники") + "</strong></p><ul>";
 page += "<li><a href=\"https://github.com/glaretechnologies/glare-core\">" + loc("Glare Core", "Glare Core") + "</a></li>";
 page += "<li><a href=\"https://www.khronos.org/\">" + loc("Khronos Group", "Khronos Group") + "</a></li>";
 page += "<li><a href=\"https://registry.khronos.org/glTF/\">" + loc("glTF", "glTF") + "</a></li>";
 page += "<li><a href=\"https://registry.khronos.org/KTX/\">" + loc("KTX", "KTX") + "</a></li>";
 page += "<li><a href=\"https://registry.khronos.org/webgl/\">" + loc("WebGL", "WebGL") + "</a></li>";
 page += "<li><a href=\"https://www.khronos.org/openxr/\">" + loc("OpenXR", "OpenXR") + "</a></li>";
 page += "<li><a href=\"https://arxiv.org/abs/2308.04079\">" + loc("3D Gaussian Splatting", "3D Gaussian Splatting") + "</a></li>";
 page += "<li><a href=\"https://metaverse-standards.org/\">" + loc("Metaverse Standards Forum", "Metaverse Standards Forum") + "</a></li>";
 page += "<li><a href=\"https://metaverse-standards.org/domain-groups/real-virtual-world-integration/\">" + loc("Real/Virtual World Integration", "Real/Virtual World Integration") + "</a></li>";
 page += "<li><a href=\"https://metaverse-standards.org/news/blog/announcing-the-web-of-worlds-whitepaper-a-concrete-path-to-the-open-metaverse/\">" + loc("Web of Worlds", "Web of Worlds") + "</a></li>";
 page += "</ul>";
 page += WebServerResponseUtils::standardFooter(request_info, true);
 web::ResponseUtils::writeHTTPOKHeaderAndData(reply_info, page);
}
void renderAboutScripting(ServerAllWorldsState& world_state, WebDataStore& data_store, const web::RequestInfo& request_info, web::ReplyInfo& reply_info)
{
	std::string page = WebServerResponseUtils::standardHeader(world_state, request_info, /*page title=*/"Historical Substrata scripting reference");
	
	Reference<WebDataStoreFile> store_file = data_store.getFragmentFile("about_scripting.htmlfrag");
	if(store_file.nonNull())
	{
		page += std::string(store_file->uncompressed_data.begin(), store_file->uncompressed_data.end());
	}

	page += WebServerResponseUtils::standardFooter(request_info, /*include_email_link=*/true);

	web::ResponseUtils::writeHTTPOKHeaderAndData(reply_info, page);
}


void renderGenericPage(ServerAllWorldsState& world_state, WebDataStore& data_store, const GenericPage& generic_page, const web::RequestInfo& request_info, web::ReplyInfo& reply_info)
{
	std::string page = WebServerResponseUtils::standardHeader(world_state, request_info, /*page title=*/generic_page.page_title);
	
	Reference<WebDataStoreFile> store_file = data_store.getFragmentFile(generic_page.fragment_path);
	if(store_file.nonNull())
	{
		page += std::string(store_file->uncompressed_data.begin(), store_file->uncompressed_data.end());
	}

	page += WebServerResponseUtils::standardFooter(request_info, /*include_email_link=*/true);

	web::ResponseUtils::writeHTTPOKHeaderAndData(reply_info, page);
}


void renderAboutSubstrataPage(ServerAllWorldsState& world_state, WebDataStore& data_store, const web::RequestInfo& request_info, web::ReplyInfo& reply_info)
{
	std::string page = WebServerResponseUtils::standardHeader(world_state, request_info, /*page title=*/"Historical Substrata documentation");

	Reference<WebDataStoreFile> store_file = data_store.getFragmentFile("about_substrata.htmlfrag");
	if(store_file.nonNull())
	{
		page += std::string(store_file->uncompressed_data.begin(), store_file->uncompressed_data.end());
	}

	page += WebServerResponseUtils::standardFooter(request_info, true);

	web::ResponseUtils::writeHTTPOKHeaderAndData(reply_info, page);
}


void renderRunningYourOwnServerPage(ServerAllWorldsState& world_state, WebDataStore& data_store, const web::RequestInfo& request_info, web::ReplyInfo& reply_info)
{
	std::string page = WebServerResponseUtils::standardHeader(world_state, request_info, /*page title=*/"Historical Substrata server guide");

	Reference<WebDataStoreFile> store_file = data_store.getFragmentFile("running_your_own_server.htmlfrag");
	if(store_file.nonNull())
	{
		page += std::string(store_file->uncompressed_data.begin(), store_file->uncompressed_data.end());
	}
	
	page += WebServerResponseUtils::standardFooter(request_info, true);

	web::ResponseUtils::writeHTTPOKHeaderAndData(reply_info, page);
}


void renderNotFoundPage(ServerAllWorldsState& world_state, const web::RequestInfo& request_info, web::ReplyInfo& reply_info)
{
	std::string page_out = WebServerResponseUtils::standardHeader(world_state, request_info, "Metasiberia");

	//---------- Right column -------------
	page_out += "<div class=\"right\">"; // right div


	//-------------- Render posts ------------------
	page_out += "Sorry, the item you were looking for does not exist at that URL.";

	page_out += "</div>"; // end right div
	page_out += WebServerResponseUtils::standardFooter(request_info, true);
	page_out += "</div>"; // main div
	page_out += "</body></html>";

	web::ResponseUtils::writeHTTPOKHeaderAndData(reply_info, page_out);
}


void renderBotStatusPage(ServerAllWorldsState& world_state, const web::RequestInfo& request_info, web::ReplyInfo& reply_info)
{
	std::string page = WebServerResponseUtils::standardHeader(world_state, request_info, /*page title=*/"Bot Status");

	{ // lock scope
		Lock lock(world_state.mutex);
		page += "<h3>Screenshot bot</h3>";
		if(world_state.last_screenshot_bot_contact_time.time == 0)
			page += "No contact from screenshot bot since last server start.";
		else
		{
			if(TimeStamp::currentTime().time - world_state.last_screenshot_bot_contact_time.time < 60)
				page += "Screenshot bot is running.  ";
			page += "Last contact from screenshot bot " + world_state.last_screenshot_bot_contact_time.timeAgoDescription();
		}

		page += "<h3>Lightmapper bot</h3>";
		if(world_state.last_lightmapper_bot_contact_time.time == 0)
			page += "No contact from lightmapper bot since last server start.";
		else
		{
			if(TimeStamp::currentTime().time - world_state.last_lightmapper_bot_contact_time.time < 60 * 10)
				page += "Lightmapper bot is running.  ";
			page += "Last contact from lightmapper bot " + world_state.last_lightmapper_bot_contact_time.timeAgoDescription();
		}

		page += "<h3>Ethereum parcel minting bot</h3>";
		if(world_state.last_eth_bot_contact_time.time == 0)
			page += "No contact from eth bot since last server start.";
		else
		{
			if(TimeStamp::currentTime().time - world_state.last_eth_bot_contact_time.time < 60 * 10)
				page += "Eth bot is running.  ";
			page += "Last contact from eth bot " + world_state.last_eth_bot_contact_time.timeAgoDescription();
		}
	}

	page += WebServerResponseUtils::standardFooter(request_info, true);

	web::ResponseUtils::writeHTTPOKHeaderAndData(reply_info, page);
}


void renderMapPage(ServerAllWorldsState& world_state, const web::RequestInfo& request_info, web::ReplyInfo& reply_info)
{
	const std::string extra_header_tags = WebServerResponseUtils::getMapHeaderTags();
	std::string page = WebServerResponseUtils::standardHeader(world_state, request_info, /*page title=*/"Map", extra_header_tags);

	std::string world_name = normaliseWorldNameField(request_info.getURLParam("world").str());
	std::string actual_world_name = world_name;

	if(!world_name.empty())
	{
		Lock lock(world_state.mutex);
		if(world_state.world_states.find(world_name) == world_state.world_states.end())
		{
			page += "<div class=\"msg\">Could not find the world '" + web::Escaping::HTMLEscape(world_name) + "'.</div>\n";
			actual_world_name = "";
		}
	}

	page += WebServerResponseUtils::getMapEmbedCode(world_state, /*highlighted_parcel_id=*/ParcelID::invalidParcelID(), actual_world_name);

	page += WebServerResponseUtils::standardFooter(request_info, true);

	web::ResponseUtils::writeHTTPOKHeaderAndData(reply_info, page);
}


} // end namespace MainPageHandlers
