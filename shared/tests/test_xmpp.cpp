#include <gtest/gtest.h>

#include <string>
#include <vector>

#include "tds/xmpp.hpp"

namespace {

std::vector<std::string> parse(const std::string& raw, std::string& tail) {
  std::string buffer = raw;
  int depth = 0;
  auto stanzas = tds::extract_xmpp_stanzas(buffer, depth);
  tail = buffer;
  return stanzas;
}

}  // namespace

TEST(XmppXml, EscapesAndUnescapes) {
  EXPECT_EQ(tds::xml_escape("a<b&c>\"d'"), "a&lt;b&amp;c&gt;&quot;d&apos;");
  EXPECT_EQ(tds::xml_unescape("a&lt;b&amp;c&gt;&quot;d&apos;"), "a<b&c>\"d'");
  // Los símbolos de la tabla v1 sobreviven el viaje por XML.
  const std::string symbols = "!ÀÁÂñÑ€";
  EXPECT_EQ(tds::xml_unescape(tds::xml_escape(symbols)), symbols);
}

TEST(XmppXml, UnescapesNumericReferencesAsUtf8) {
  // Algunos servidores escapan los símbolos de 2 bytes de la tabla v1 como
  // referencias numéricas; deben recuperarse como UTF-8, no perderse.
  EXPECT_EQ(tds::xml_unescape("&#233;"), "\xC3\xA9");      // é decimal
  EXPECT_EQ(tds::xml_unescape("&#xE9;"), "\xC3\xA9");      // é hexadecimal
  EXPECT_EQ(tds::xml_unescape("&#x20AC;"), "\xE2\x82\xAC");  // €
  EXPECT_EQ(tds::xml_unescape("&#0;"), "");                // NUL no válido
  EXPECT_EQ(tds::xml_unescape("&#xD800;"), "");            // subrogado inválido
}

TEST(XmppXml, ExtractsMessagesInsideStream) {
  const std::string wire =
      "<?xml version='1.0'?><stream:stream xmlns='jabber:client' "
      "xmlns:stream='http://etherx.jabber.org/streams'>"
      "<message from='a@im.todus.cu' to='b@im.todus.cu' type='chat' id='m1'>"
      "<body>TDS1|job|tr|0|1|1|00000000|!À</body></message>"
      "<tdack mi='m1'/>"
      "<presence/>";
  std::string tail;
  const auto stanzas = parse(wire, tail);
  ASSERT_EQ(stanzas.size(), 3u);
  EXPECT_EQ(tds::xml_local_name(stanzas[0]), "message");
  EXPECT_EQ(tds::xml_attribute(stanzas[0], "id"), "m1");
  EXPECT_EQ(tds::xml_attribute(stanzas[0], "from"), "a@im.todus.cu");
  EXPECT_EQ(tds::xml_local_name(stanzas[1]), "tdack");
  EXPECT_EQ(tds::xml_attribute(stanzas[1], "mi"), "m1");
  EXPECT_TRUE(tail.empty());
}

TEST(XmppXml, NormalizesAbbreviatedServerStanzas) {
  const std::string wire =
      "<stream:stream xmlns='jabber:client' xmlns:stream='http://etherx.jabber.org/streams'>"
      "<m f='a@im.todus.cu' o='b@im.todus.cu' t='chat' i='x1'><b>hola</b></m>";
  std::string tail;
  const auto stanzas = parse(wire, tail);
  ASSERT_EQ(stanzas.size(), 1u);
  EXPECT_EQ(tds::xml_local_name(stanzas[0]), "message");
  EXPECT_EQ(tds::xml_attribute(stanzas[0], "id"), "x1");
  EXPECT_EQ(tds::xml_attribute(stanzas[0], "to"), "b@im.todus.cu");
  EXPECT_EQ(tds::xml_attribute(stanzas[0], "type"), "chat");
}

TEST(XmppXml, KeepsIncompleteStanzaForNextRead) {
  const std::string wire =
      "<stream:stream xmlns='jabber:client' xmlns:stream='http://etherx.jabber.org/streams'>"
      "<message id='m1'><body>hola</body>";
  std::string tail;
  const auto stanzas = parse(wire, tail);
  EXPECT_TRUE(stanzas.empty());
  EXPECT_NE(tail.find("<message"), std::string::npos);

  // Al completarse en la siguiente lectura aparece la stanza.
  tail += "</message>";
  int depth = 1;
  const auto more = tds::extract_xmpp_stanzas(tail, depth);
  ASSERT_EQ(more.size(), 1u);
  EXPECT_EQ(tds::xml_attribute(more[0], "id"), "m1");
}

TEST(XmppXml, HandlesNestedElementsAndCdata) {
  const std::string wire =
      "<stream:stream xmlns='jabber:client' xmlns:stream='http://etherx.jabber.org/streams'>"
      "<message id='n1'><body><![CDATA[TDS1|a|b|0|1|1|00000000|!]]></body>"
      "<x xmlns='todus:x'><a><b/></a></x></message>";
  std::string tail;
  const auto stanzas = parse(wire, tail);
  ASSERT_EQ(stanzas.size(), 1u);
  EXPECT_EQ(tds::xml_attribute(stanzas[0], "id"), "n1");
  EXPECT_NE(stanzas[0].find("TDS1|a|b"), std::string::npos);
}

TEST(XmppXml, DetectsClosedStream) {
  std::string buffer =
      "<stream:stream xmlns='jabber:client' xmlns:stream='http://etherx.jabber.org/streams'>"
      "<message id='m1'><body>x</body></message></stream:stream>";
  int depth = 0;
  tds::extract_xmpp_stanzas(buffer, depth);
  // El cierre deja el parser fuera del stream: no inventa stanzas.
  EXPECT_EQ(depth, 0);
}
