require 'asciidoctor/extensions'

Asciidoctor::Extensions.register do

  inline_macro :linkgit do
    process do |parent, target, attrs|
      if parent.document.doctype == 'book'
        "<ulink url=\"#{target}.html\">" \
        "#{target}(#{attrs[1]})</ulink>"
      elsif parent.document.basebackend? 'html'
        prefix = parent.document.attr('git-relative-html-prefix')
        %(<a href="#{prefix}#{target}.html">#{target}(#{attrs[1]})</a>)
      elsif parent.document.basebackend? 'docbook'
        "<citerefentry>\n" \
          "<refentrytitle>#{target}</refentrytitle>" \
          "<manvolnum>#{attrs[1]}</manvolnum>\n" \
        "</citerefentry>"
      end
    end
  end

end
