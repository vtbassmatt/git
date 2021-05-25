require 'asciidoctor/extensions'

Asciidoctor::Extensions.register do

  doc = document

  inline_macro :linkgit do
    if doc.doctype == 'book'
      process do |parent, target, attrs|
        "<ulink url=\"#{target}.html\">" \
        "#{target}(#{attrs[1]})</ulink>"
      end
    elsif doc.basebackend? 'html'
      prefix = doc.attr('git-relative-html-prefix')
      process do |parent, target, attrs|
        %(<a href="#{prefix}#{target}.html">#{target}(#{attrs[1]})</a>)
      end
    elsif doc.basebackend? 'docbook'
      process do |parent, target, attrs|
        "<citerefentry>\n" \
          "<refentrytitle>#{target}</refentrytitle>" \
          "<manvolnum>#{attrs[1]}</manvolnum>\n" \
        "</citerefentry>"
      end
    end
  end

end
