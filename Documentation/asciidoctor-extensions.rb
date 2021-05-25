require 'asciidoctor/extensions'

Asciidoctor::Extensions.register do

  doc = document

  inline_macro :linkgit do
    if doc.doctype == 'book'
      process do |parent, target, attrs|
        '<ulink url="%1$s.html">%1$s(%2$s)</ulink>' % [target, attrs[1]]
      end
    elsif doc.basebackend? 'html'
      prefix = doc.attr('git-relative-html-prefix')
      process do |parent, target, attrs|
        %(<a href="#{prefix}%1$s.html">%1$s(%2$s)</a>) % [target, attrs[1]]
      end
    elsif doc.basebackend? 'docbook'
      process do |parent, target, attrs|
        <<~EOF.chomp % [target, attrs[1]]
        <citerefentry>
        <refentrytitle>%s</refentrytitle><manvolnum>%s</manvolnum>
        </citerefentry>
        EOF
      end
    end
  end

end
