<?xml version='1.0'?> <!--*- mode: xml -*-->
<xsl:stylesheet xmlns:xsl="http://www.w3.org/1999/XSL/Transform"  xmlns="http://www.w3.org/1999/xhtml" 
                version="1.0">
  <!-- import the chunked XSL stylesheet -->
  <xsl:import href="http://docbook.sourceforge.net/release/xsl/current/xhtml/chunk.xsl"/>
  <xsl:include href="/usr/share/gtk-doc/data/devhelp.xsl"/>
  <xsl:include href="/usr/share/gtk-doc/data/devhelp2.xsl"/>
  <xsl:include href="/usr/share/gtk-doc/data/version-greater-or-equal.xsl"/>

  <!-- change some parameters -->
  <xsl:param name="toc.section.depth">2</xsl:param>
  <xsl:param name="generate.toc">
    book	toc
    chapter	toc
  </xsl:param>

  
  <xsl:param name="default.encoding" select="'US-ASCII'"/>
  <xsl:param name="chunker.output.encoding" select="'US-ASCII'"/>
  <xsl:param name="chunker.output.indent" select="'yes'"/>
  <xsl:param name="chunker.output.doctype-public" select="'-//W3C//DTD XHTML 1.0 Transitional//EN'"/>
  <xsl:param name="chunk.fast" select="1"/> 
  <xsl:param name="chapter.autolabel" select="0"/>
  <xsl:param name="use.id.as.filename" select="1"/>
  <xsl:param name="html.ext" select="'.xhtml'"/>
  <xsl:param name="refentry.generate.name" select="0"/>
  <xsl:param name="refentry.generate.title" select="1"/>

  <!-- use index filtering (if available) -->
  <xsl:param name="index.on.role" select="1"/>

  <!-- display variablelists as tables -->
  <xsl:param name="variablelist.as.table" select="1"/>

  <!-- this gets set on the command line ... -->
  <xsl:param name="gtkdoc.version" select="''"/>
  <xsl:param name="gtkdoc.bookname" select="''"/>

  <!-- ========================================================= -->
  <!-- template to create the index.sgml anchor index -->

  <xsl:template match="book|article">
    <xsl:variable name="tooldver">
      <xsl:call-template name="version-greater-or-equal">
        <xsl:with-param name="ver1" select="$VERSION" />
        <xsl:with-param name="ver2">1.36</xsl:with-param>
      </xsl:call-template>
    </xsl:variable>
    <xsl:if test="$tooldver = 0">
      <xsl:message terminate="yes">
FATAL-ERROR: You need the DocBook XSL Stylesheets version 1.36 or higher
to build the documentation. 
Get a newer version at http://docbook.sourceforge.net/projects/xsl/
      </xsl:message>
    </xsl:if>
    <xsl:apply-imports/>

    <!-- generate the index.sgml href index -->
    <xsl:call-template name="generate.index"/>
    <xsl:call-template name="generate.devhelp"/>
    <xsl:call-template name="generate.devhelp2"/>
  </xsl:template>

  <xsl:template name="generate.index">
    <xsl:call-template name="write.text.chunk">
      <xsl:with-param name="filename" select="'index.sgml'"/>
      <xsl:with-param name="content">
        <!-- check all anchor and refentry elements -->
        <xsl:apply-templates select="//anchor|//refentry"
                             mode="generate.index.mode"/>
      </xsl:with-param>
      <xsl:with-param name="encoding" select="'utf-8'"/>
    </xsl:call-template>
  </xsl:template>

  <xsl:template match="*" mode="generate.index.mode">
    <xsl:if test="not(@href)">
      <xsl:text>&lt;ANCHOR id=&quot;</xsl:text>
      <xsl:value-of select="@id"/>
      <xsl:text>&quot; href=&quot;</xsl:text>
      <xsl:if test="$gtkdoc.bookname">
        <xsl:value-of select="$gtkdoc.bookname"/>
        <xsl:text>/</xsl:text>
      </xsl:if>
      <xsl:call-template name="href.target"/>
      <xsl:text>&quot;&gt;
</xsl:text>
    </xsl:if>
  </xsl:template>

  <!-- ========================================================= -->
  <!-- template to output gtkdoclink elements for the unknown targets -->

  <xsl:template match="link">
    <xsl:choose>
      <xsl:when test="id(@linkend)">
        <xsl:apply-imports/>
      </xsl:when>
      <xsl:otherwise>
        <GTKDOCLINK HREF="{@linkend}">
          <xsl:apply-templates/>
        </GTKDOCLINK>
      </xsl:otherwise>
    </xsl:choose>
  </xsl:template>

  <!-- ========================================================= -->
  <!-- Below are the visual portions of the stylesheet.  They provide
       the normal gtk-doc output style. -->

  <xsl:param name="shade.verbatim" select="0"/>
  <xsl:param name="refentry.separator" select="0"/>

  <xsl:template match="refsect2">
    <xsl:if test="preceding-sibling::refsect2">
      <hr/>
    </xsl:if>
    <xsl:apply-imports/>
  </xsl:template>

  <xsl:template name="user.head.content">
    <xsl:if test="$gtkdoc.version">
      <meta name="generator"
            content="GTK-Doc V{$gtkdoc.version} (XML mode)"/>
    </xsl:if>
    <link rel="stylesheet" href="style.css" type="text/css"/>

      <!-- copied from the html.head template in the docbook stylesheets
           we don't want links for all refentrys, thats just too much
        -->
      <xsl:variable name="this" select="."/>
      <xsl:for-each select="//part
                            |//reference
                            |//preface
                            |//chapter
                            |//article
                            |//appendix[not(parent::article)]|appendix
                            |//glossary[not(parent::article)]|glossary
                            |//index[not(parent::article)]|index">
        <link rel="{local-name(.)}">
          <xsl:attribute name="href">
            <xsl:call-template name="href.target">
              <xsl:with-param name="context" select="$this"/>
              <xsl:with-param name="object" select="."/>
            </xsl:call-template>
          </xsl:attribute>
          <xsl:attribute name="title">
            <xsl:apply-templates select="." mode="object.title.markup.textonly"/>
          </xsl:attribute>
        </link>
      </xsl:for-each>
  </xsl:template>

  <xsl:template match="title" mode="book.titlepage.recto.mode">
    <table class="navigation" id="top" width="100%"
           cellpadding="2" cellspacing="0">
      <tr>
        <th valign="middle">
          <p class="{name(.)}">
            <xsl:value-of select="."/>
          </p>
        </th>
      </tr>
    </table>
  </xsl:template>

  <xsl:template name="header.navigation">
    <xsl:param name="prev" select="/foo"/>
    <xsl:param name="next" select="/foo"/>
    <xsl:variable name="home" select="/*[1]"/>
    <xsl:variable name="up" select="parent::*"/>
    <xsl:variable name="sections" select="./refsect1[@role]"/>
    <xsl:variable name="sect_object_hierarchy" select="./refsect1[@role='object_hierarchy']"/>
    <xsl:variable name="sect_prerequisites" select="./refsect1[@role='prerequisites']"/>
    <xsl:variable name="sect_derived_interfaces" select="./refsect1[@role='derived_interfaces']"/>
    <xsl:variable name="sect_impl_interfaces" select="./refsect1[@role='impl_interfaces']"/>
    <xsl:variable name="sect_implementations" select="./refsect1[@role='implementations']"/>
    <xsl:variable name="sect_properties" select="./refsect1[@role='properties']"/>
    <xsl:variable name="sect_child_properties" select="./refsect1[@role='child_properties']"/>
    <xsl:variable name="sect_style_properties" select="./refsect1[@role='style_properties']"/>
    <xsl:variable name="sect_signal_proto" select="./refsect1[@role='signal_proto']"/>
    <xsl:variable name="sect_desc" select="./refsect1[@role='desc']"/>
    <xsl:variable name="sect_synopsis" select="./refsynopsisdiv[@role='synopsis']"/>
    <!--
    <xsl:variable name="sect_details" select="./refsect1[@id='details']"/>
    <xsl:variable name="sect_property_details" select="./refsect1[@id='property_details']"/>
    <xsl:variable name="sect_child_property_details" select="./refsect1[@id='child_property_details']"/>
    <xsl:variable name="sect_style_property_details" select="./refsect1[@id='style_property_details']"/>
    <xsl:variable name="sect_signals" select="./refsect1[@id='signals']"/>
    -->

    <xsl:if test="$suppress.navigation = '0' and $home != .">
      <table class="navigation" id="top" width="100%"
             summary = "Navigation header" cellpadding="2" cellspacing="2">
        <tr valign="middle">
          <xsl:choose>
            <xsl:when test="count($prev) > 0">
              <td>
                <a accesskey="p">
                  <xsl:attribute name="href">
                    <xsl:call-template name="href.target">
                      <xsl:with-param name="object" select="$prev"/>
                    </xsl:call-template>
                  </xsl:attribute>
                  <img src="left.png" width="24" height="24" border="0">
                    <xsl:attribute name="alt">
                      <xsl:call-template name="gentext">
                        <xsl:with-param name="key">nav-prev</xsl:with-param>
                      </xsl:call-template>
                    </xsl:attribute>
                  </img>
                </a>
              </td>
            </xsl:when>
            <xsl:otherwise>
              <td>&#160;</td>
            </xsl:otherwise>
          </xsl:choose>
          <xsl:choose>
            <xsl:when test="count($up) > 0 and $up != $home">
              <td>
                <a accesskey="u">
                  <xsl:attribute name="href">
                    <xsl:call-template name="href.target">
                      <xsl:with-param name="object" select="$up"/>
                    </xsl:call-template>
                  </xsl:attribute>
                  <img src="up.png" width="24" height="24" border="0">
                    <xsl:attribute name="alt">
                      <xsl:call-template name="gentext">
                        <xsl:with-param name="key">nav-up</xsl:with-param>
                      </xsl:call-template>
                    </xsl:attribute>
                  </img>
                </a>
              </td>
            </xsl:when>
            <xsl:otherwise>
              <td>&#160;</td>
            </xsl:otherwise>
          </xsl:choose>
          <xsl:choose>
            <xsl:when test="$home != .">
              <td>
                <a accesskey="h">
                  <xsl:attribute name="href">
                    <xsl:call-template name="href.target">
                      <xsl:with-param name="object" select="$home"/>
                    </xsl:call-template>
                  </xsl:attribute>
                  <img src="home.png" width="24" height="24" border="0">
                    <xsl:attribute name="alt">
                      <xsl:call-template name="gentext">
                        <xsl:with-param name="key">nav-home</xsl:with-param>
                      </xsl:call-template>
                    </xsl:attribute>
                  </img>
                </a>
              </td>
            </xsl:when>
            <xsl:otherwise>
              <td>&#160;</td>
            </xsl:otherwise>
          </xsl:choose>
          <th width="100%" align="center">
            <xsl:apply-templates select="$home" mode="object.title.markup"/>
          </th>
          <xsl:choose>
            <xsl:when test="count($next) > 0">
              <td>
                <a accesskey="n">
                  <xsl:attribute name="href">
                    <xsl:call-template name="href.target">
                      <xsl:with-param name="object" select="$next"/>
                    </xsl:call-template>
                  </xsl:attribute>
                  <img src="right.png" width="24" height="24" border="0">
                    <xsl:attribute name="alt">
                      <xsl:call-template name="gentext">
                        <xsl:with-param name="key">nav-next</xsl:with-param>
                      </xsl:call-template>
                    </xsl:attribute>
                  </img>
                </a>
              </td>
            </xsl:when>
            <xsl:otherwise>
              <td>&#160;</td>
            </xsl:otherwise>
          </xsl:choose>
        </tr>
        <!--<xsl:if test="name()='refentry'"-->
        <xsl:if test="count($sections) > 0">
          <tr>
            <td colspan="5" class="shortcuts">
              <nobr>
                <xsl:if test="count($sect_synopsis) > 0">
                  <a href="#{generate-id(.//*[@role='top_of_page'])}" class="shortcut">Top</a>
                </xsl:if>
                <xsl:if test="count($sect_desc) > 0">
                  &#160;|&#160;
                  <a href="#{generate-id(./refsect1[@role='desc'])}" class="shortcut">
                    <xsl:value-of select="./refsect1[@role='desc']/title"/>
                  </a>
                </xsl:if>
                <xsl:if test="count($sect_object_hierarchy) > 0">
                  &#160;|&#160;
                  <a href="#{generate-id(./refsect1[@role='object_hierarchy'])}" class="shortcut">
                    <xsl:value-of select="./refsect1[@role='object_hierarchy']/title"/>
                  </a>
                </xsl:if>
                <xsl:if test="count($sect_impl_interfaces) > 0">
                  &#160;|&#160;
                  <a href="#{generate-id(./refsect1[@role='impl_interfaces'])}" class="shortcut">
                    <xsl:value-of select="./refsect1[@role='impl_interfaces']/title"/>
                  </a>
                </xsl:if>
                <xsl:if test="count($sect_prerequisites) > 0">
                  &#160;|&#160;
                  <a href="#{generate-id(./refsect1[@role='object_prerequisites'])}" class="shortcut">
                    <xsl:value-of select="./refsect1[@role='object_prerequisites']/title"/>
                  </a>
                </xsl:if>
                <xsl:if test="count($sect_derived_interfaces) > 0">
                  &#160;|&#160;
                  <a href="#{generate-id(./refsect1[@role='derived_interfaces'])}" class="shortcut">
                    <xsl:value-of select="./refsect1[@role='derived_interfaces']/title"/>
                  </a>
                </xsl:if>
                <xsl:if test="count($sect_implementations) > 0">
                  &#160;|&#160;
                  <a href="#{generate-id(./refsect1[@role='implementations'])}" class="shortcut">
                    <xsl:value-of select="./refsect1[@role='implementations']/title"/>
                  </a>
                </xsl:if>
                <xsl:if test="count($sect_properties) > 0">
                  &#160;|&#160;
                  <a href="#{generate-id(./refsect1[@role='properties'])}" class="shortcut">
                    <xsl:value-of select="./refsect1[@role='properties']/title"/>
                  </a>
                </xsl:if>
                <xsl:if test="count($sect_child_properties) > 0">
                  &#160;|&#160;
                  <a href="#{generate-id(./refsect1[@role='child_properties'])}" class="shortcut">
                    <xsl:value-of select="./refsect1[@role='child_properties']/title"/>
                  </a>
                </xsl:if>
                <xsl:if test="count($sect_style_properties) > 0">
                  &#160;|&#160;
                  <a href="#{generate-id(./refsect1[@role='style_properties'])}" class="shortcut">
                    <xsl:value-of select="./refsect1[@role='style_properties']/title"/>
                  </a>
                </xsl:if>
                <xsl:if test="count($sect_signal_proto) > 0">
                  &#160;|&#160;
                  <a href="#{generate-id(./refsect1[@role='signal_proto'])}" class="shortcut">
                    <xsl:value-of select="./refsect1[@role='signal_proto']/title"/>
                  </a>
                </xsl:if>
                <!--
                <xsl:if test="count($sect_details) > 0">
                  <a href="#details" class="shortcut">
                    <xsl:value-of select="./refsect1[@id='details']/title"/>
                  </a>
                  &#160;|&#160;
                </xsl:if>
                <xsl:if test="count($sect_property_details) > 0">
                  <a href="#property_details" class="shortcut">
                    <xsl:value-of select="./refsect1[@id='property_details']/title"/>
                  </a>
                  &#160;|&#160;
                </xsl:if>
                <xsl:if test="count($sect_child_property_details) > 0">
                  <a href="#child_property_details" class="shortcut">
                    <xsl:value-of select="./refsect1[@id='property_child_details']/title"/>
                  </a>
                  &#160;|&#160;
                </xsl:if>
                <xsl:if test="count($sect_style_property_details) > 0">
                  <a href="#style_property_details" class="shortcut">
                    <xsl:value-of select="./refsect1[@id='style_property_details']/title"/>
                  </a>
                  &#160;|&#160;
                </xsl:if>
                <xsl:if test="count($sect_signals) > 0">
                  <a href="#signals" class="shortcut">
                    <xsl:value-of select="./refsect1[@id='signals']/title"/>
                  </a>
                  &#160;|&#160;
                </xsl:if>
                -->
              </nobr>
            </td>
          </tr>
        </xsl:if>
      </table>
    </xsl:if>
  </xsl:template>

  <xsl:template name="footer.navigation">
  </xsl:template>

  <!-- avoid creating multiple identical indices 
       if the stylesheets don't support filtered indices
    -->
  <xsl:template match="index"> 
    <xsl:variable name="has-filtered-index">
      <xsl:call-template name="version-greater-or-equal">
        <xsl:with-param name="ver1" select="$VERSION" />
        <xsl:with-param name="ver2">1.66</xsl:with-param>
      </xsl:call-template>
    </xsl:variable>
    <xsl:if test="($has-filtered-index = 1) or (count(@role) = 0)">
      <xsl:apply-imports/>
    </xsl:if> 
  </xsl:template>

  <xsl:template match="index" mode="toc">
    <xsl:variable name="has-filtered-index">
      <xsl:call-template name="version-greater-or-equal">
        <xsl:with-param name="ver1" select="$VERSION" />
        <xsl:with-param name="ver2">1.66</xsl:with-param>
      </xsl:call-template>
    </xsl:variable>
    <xsl:if test="($has-filtered-index = 1) or (count(@role) = 0)">
      <xsl:apply-imports/>
    </xsl:if> 
  </xsl:template>
 
  <xsl:template match="para">
    <xsl:choose>
      <xsl:when test="@role = 'gallery'">
         <div class="container">
           <div class="gallery-spacer"> </div>
           <xsl:apply-templates mode="gallery.mode"/>
         <div class="gallery-spacer"> </div>
         </div>
      </xsl:when>
      <xsl:otherwise>
        <xsl:apply-imports/>
      </xsl:otherwise> 
    </xsl:choose>
  </xsl:template>

  <xsl:template match="link" mode="gallery.mode">
    <div class="gallery-float">
       <xsl:apply-templates select="."/>
    </div>
  </xsl:template>

  <!-- add gallery handling to refnamediv template -->
  <xsl:template match="refnamediv">
    <div class="{name(.)}">
      <table width="100%">
        <tr><td valign="top">
         <xsl:call-template name="anchor"/>
           <xsl:choose>
             <xsl:when test="$refentry.generate.name != 0">
               <h2>
                <xsl:call-template name="gentext">
                  <xsl:with-param name="key" select="'RefName'"/>
                </xsl:call-template>
              </h2>
            </xsl:when>
            <xsl:when test="$refentry.generate.title != 0">
              <h2>
                <xsl:choose>
                  <xsl:when test="../refmeta/refentrytitle">
                    <xsl:apply-templates select="../refmeta/refentrytitle"/>
                  </xsl:when>
                  <xsl:otherwise>
                    <xsl:apply-templates select="refname[1]"/>
                  </xsl:otherwise>
                </xsl:choose>
              </h2>
            </xsl:when>
          </xsl:choose>
          <p>
            <xsl:apply-templates/>
          </p>
        </td>
        <td valign="top" align="right">
           <!-- find the gallery image to use here 
                - determine the id of the enclosing refentry
                - look for an inlinegraphic inside a link with linkend == refentryid inside a para with role == gallery
                - use it here
             -->
           <xsl:variable name="refentryid" select="../@id"/>
           <xsl:apply-templates select="//para[@role = 'gallery']/link[@linkend = $refentryid]/inlinegraphic"/>
        </td></tr>
       </table>
     </div>
  </xsl:template>

  <!-- Add anchor with ID. Note that elements such as refsect1 don't need this
       since the docbook stylesheets should add it -->
  <xsl:template match="*[@role='top_of_page']">
    <a name="{generate-id(.)}"/>
    <xsl:apply-imports/>
  </xsl:template>
  
</xsl:stylesheet>
