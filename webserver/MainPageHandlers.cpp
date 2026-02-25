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
	const std::string root_page_description = "Metaverse from Siberia. Craft virtually - Inspire reality.";
	const std::string root_page_url = "https://vr.metasiberia.com/";
	const std::string root_page_image_url = "https://vr.metasiberia.com/files/main.jpg";

	const std::string root_page_meta_tags =
		"\t\t<link rel=\"canonical\" href=\"" + root_page_url + "\" />\n"
		"\t\t<meta name=\"description\" content=\"" + web::Escaping::HTMLEscape(root_page_description) + "\" />\n"
		"\t\t<meta name=\"robots\" content=\"index,follow,max-image-preview:large,max-snippet:-1,max-video-preview:-1\" />\n"
		"\t\t<meta property=\"og:site_name\" content=\"Metasiberia\" />\n"
		"\t\t<meta property=\"og:type\" content=\"website\" />\n"
		"\t\t<meta property=\"og:title\" content=\"" + web::Escaping::HTMLEscape(root_page_title) + "\" />\n"
		"\t\t<meta property=\"og:description\" content=\"" + web::Escaping::HTMLEscape(root_page_description) + "\" />\n"
		"\t\t<meta property=\"og:url\" content=\"" + root_page_url + "\" />\n"
		"\t\t<meta property=\"og:image\" content=\"" + root_page_image_url + "\" />\n"
		"\t\t<meta property=\"og:image:secure_url\" content=\"" + root_page_image_url + "\" />\n"
		"\t\t<meta property=\"og:image:type\" content=\"image/jpeg\" />\n"
		"\t\t<meta property=\"og:image:width\" content=\"543\" />\n"
		"\t\t<meta property=\"og:image:height\" content=\"608\" />\n"
		"\t\t<meta property=\"og:image:alt\" content=\"Metasiberia - Metaverse from Siberia\" />\n"
		"\t\t<meta name=\"twitter:card\" content=\"summary_large_image\" />\n"
		"\t\t<meta name=\"twitter:title\" content=\"" + web::Escaping::HTMLEscape(root_page_title) + "\" />\n"
		"\t\t<meta name=\"twitter:description\" content=\"" + web::Escaping::HTMLEscape(root_page_description) + "\" />\n"
		"\t\t<meta name=\"twitter:image\" content=\"" + root_page_image_url + "\" />\n"
		"\t\t<script type=\"application/ld+json\">{\"@context\":\"https://schema.org\",\"@type\":\"WebSite\",\"name\":\"Metasiberia\",\"url\":\"https://vr.metasiberia.com/\",\"description\":\"Metaverse from Siberia. Craft virtually - Inspire reality.\",\"image\":\"https://vr.metasiberia.com/files/main.jpg\"}</script>\n";

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


		//------------------------------- Build photos grid view HTML --------------------------
		photos_html.reserve(4096);
		photos_html += "<div class=\"photo-container\">\n";		const int max_num_photos_to_display = 20;
		int num_photos_displayed = 0;
		for(auto it = world_state.photos.rbegin(); (it != world_state.photos.rend()) && (num_photos_displayed < max_num_photos_to_display); ++it)
		{
			const Photo* photo = it->second.ptr();
			if(photo->state == Photo::State_published)
			{
				photos_html += "<a href=\"/photo/";
				photos_html += toString(photo->id);
				photos_html += "\"><img src=\"/photo_thumb_image/";
				photos_html += toString(photo->id);
				photos_html += "\" class=\"root-photo-img\" title=\"";
				photos_html += web::Escaping::HTMLEscape(photo->caption);
				photos_html += "\"/></a>";

				num_photos_displayed++;
			}
		}

		photos_html += "</div>\n";

	} // end lock scope


	Reference<WebDataStoreFile> store_file = data_store.getFragmentFile("root_page.htmlfrag");
	if(store_file.nonNull())
	{
		page_out += std::string(store_file->uncompressed_data.begin(), store_file->uncompressed_data.end());
	}

	StringUtils::replaceFirstInPlace(page_out, "LATEST_NEWS_HTML", latest_news_html);

	StringUtils::replaceFirstInPlace(page_out, "LAND_PARCELS_FOR_SALE_HTML", auction_html);

	StringUtils::replaceFirstInPlace(page_out, "EVENTS_HTML", events_html);

	StringUtils::replaceFirstInPlace(page_out, "PHOTOS_HTML", photos_html);

	page_out += "<script src=\"/files/root-page.js\"></script>";
	
	page_out += WebServerResponseUtils::standardFooter(request_info, true);

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
	std::string page = WebServerResponseUtils::standardHeader(world_state, request_info, /*page title=*/"Parcel sales in Substrata");

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
	std::string page = WebServerResponseUtils::standardHeader(world_state, request_info, /*page title=*/"Основные вопросы о Metasiberia");

	page += "<h1>Основные вопросы о Metasiberia</h1>";

	page += "<h2>Что такое Metasiberia?</h2>";
	page += "<p>Metasiberia — это метавселеная. Здесь пользователи могут исследовать бескрайние просторы, общаться, играть и создавать свои уникальные территории в метавселенной.</p>";

	page += "<h2>Как войти в Метасибирь?</h2>";
	page += "<p>Адрес в клиенте: <a href=\"sub://vr.metasiberia.com\">sub://vr.metasiberia.com</a></p>";
	page += "<p>Веб-режим: <a href=\"https://vr.metasiberia.com/webclient\">https://vr.metasiberia.com/webclient</a></p>";
	page += "<p>Зарегистрируйтесь: <a href=\"https://vr.metasiberia.com/signup\">https://vr.metasiberia.com/signup</a></p>";
	page += "<ol>";
	page += "<li>Скачайте и установите клиент виртуальных миров Substrata: Тут.</li>";
	page += "<li>В адресной строке введите <code>sub://vr.metasiberia.com</code> и нажмите Enter.</li>";
	page += "<li>В клиенте нажмите Log in (правый верхний угол) и введите логин/пароль.</li>";
	page += "<li>Назначьте Metasiberia стартовой локацией: в меню Go выберите Set current location as start location.</li>";
	page += "<li>Теперь ваш аватар всегда будет появляться в центре Metasiberia.</li>";
	page += "</ol>";

	page += "<h2>На чём основана Metasiberia?</h2>";
	page += "<p>Metasiberia — это проект, основанный на технологиях Substrata, разработанных Glare Technologies Limited. Эта же команда известна своими продуктами Indigo Renderer и Chaotica Fractals.</p>";

	page += "<h2>Почему Metasiberia — это метавселенная?</h2>";
	page += "<p>Metasiberia классифицируется как метавселенная, поскольку представляет собой интегрированную цифровую среду, объединяющую множество виртуальных пространств с высоким уровнем интерактивности и пользовательской автономии. С научной точки зрения, метавселенная — это устойчивая, коллективно используемая виртуальная реальность, которая функционирует как параллельная система, поддерживающая социальные, творческие и экономические взаимодействия. В Metasiberia это достигается через центральный мир, выступающий хабом для новых пользователей, и персональные территории (доступные по адресу sub://vr.metasiberia.com/имя_пользователя), где каждый может формировать собственное пространство.</p>";

	page += "<h2>Как приобрести участок в Metasiberia?</h2>";
	page += "<p>На данный момент покупка участков доступна только по индивидуальному запросу.</p>";
	page += "<p>В ближайшее время мы запустим магазин, где участки можно будет приобрести напрямую. Следите за обновлениями на нашем сайте и в социальных сетях.</p>";

	page += "<h2>Можно ли делиться правами на участок в Metasiberia?</h2>";
	page += "<p>Да, вы можете добавить других пользователей как «соавторов» вашего участка. Они смогут создавать, редактировать и удалять объекты на вашей территории.</p>";
	page += "<ol>";
	page += "<li>Войдите в аккаунт на сайте Metasiberia / admin panel / log in.</li>";
	page += "<li>Перейдите к вашему участку на своей странице.</li>";
	page += "<li>Нажмите Add writer и укажите имя пользователя в Metasiberia.</li>";
	page += "<li>Также можно временно или постоянно открыть участок для общего редактирования, выбрав в редакторе объектов опцию All writeable.</li>";
	page += "</ol>";

	page += "<h1>Создание и строительство в Metasiberia</h1>";
	page += "<h2>Как создавать объекты в Metasiberia?</h2>";
	page += "<p>Создавать объекты в центральном мире можно на участке, который вам принадлежит, или в песочнице (участок №31). В своей личной территории ограничений нет.</p>";
	page += "<p>Есть два способа:</p>";
	page += "<ol>";
	page += "<li>В клиенте выберите «Добавить модель / изображение / видео» и следуйте инструкциям.</li>";
	page += "<li>Создайте объект из вокселей внутри Metasiberia, выбрав «Добавить воксели».</li>";
	page += "</ol>";
	page += "<p>Поддерживаемые форматы:</p>";
	page += "<p>Модели: OBJ, GLTF, GLB, VOX, STL, IGMESH</p>";
	page += "<p>Изображения: JPG, PNG, GIF, TIF, EXR, KTX, KTX2</p>";
	page += "<p>Видео: MP4</p>";
	page += "<p>Пожалуйста, избегайте загрузки моделей с большим количеством полигонов или объёмных файлов, чтобы не ухудшить производительность для других пользователей.</p>";

	page += "<h2>Как работают воксели в Metasiberia?</h2>";
	page += "<p>Вы можете загрузить готовый воксель в поддерживаемом формате или создать его внутри Metasiberia.</p>";
	page += "<p>Для создания:</p>";
	page += "<ol>";
	page += "<li>Перейдите на участок, где у вас есть права редактирования, и выберите «Добавить воксели». Появится первый блок (серый куб).</li>";
	page += "<li>Всплывёт подсказка с инструкциями. Основное: Ctrl + левый клик добавляет новый воксель на поверхность выбранного куба. При наведении с зажатым Ctrl вы увидите, где появится новый блок.</li>";
	page += "<li>Воксели можно масштабировать в редакторе, меняя их размеры через параметр Scale.</li>";
	page += "</ol>";

	page += "<h2>Как анимировать объекты в Metasiberia?</h2>";
	page += "<p>Анимация и скрипты в Metasiberia создаются с помощью языка программирования Lua. Подробности доступны на странице: <a href=\"/about_scripting\">о скриптах</a>.</p>";
	page += "<p>Для настройки скрипта выберите объект в клиенте, откройте редактор и в разделе Script введите код. Редактировать можно только свои объекты.</p>";

	page += "<h2>Есть ли ограничения на контент в Metasiberia?</h2>";
	page += "<p>Да, контент «не для всех» (например, с сексуальным или насильственным подтекстом) запрещён. Подробности — в <a href=\"/terms\">правилах использования</a>.</p>";

	page += "<h1>Решение проблем</h1>";
	page += "<h2>У меня сложности с Metasiberia — где я могу получить помощь?</h2>";
	page += "<p>Лучше всего обратиться за помощью в наш <a href=\"https://vk.com/metasiberia_official\">VK</a> или <a href=\"https://t.me/metasiberia_channel\">Telegram</a>.</p>";

	page += "<p><b>Metasiberia состоит из Substrata.</b></p>";

	page += WebServerResponseUtils::standardFooter(request_info, true);

	web::ResponseUtils::writeHTTPOKHeaderAndData(reply_info, page);
}


void renderAboutScripting(ServerAllWorldsState& world_state, WebDataStore& data_store, const web::RequestInfo& request_info, web::ReplyInfo& reply_info)
{
	std::string page = WebServerResponseUtils::standardHeader(world_state, request_info, /*page title=*/"Scripting in Substrata");
	
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
	std::string page = WebServerResponseUtils::standardHeader(world_state, request_info, /*page title=*/"Our vision for Substrata");

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
	std::string page = WebServerResponseUtils::standardHeader(world_state, request_info, /*page title=*/"Running your own Substrata Server");

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
