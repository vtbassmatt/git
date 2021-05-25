require 'asciidoctor/extensions'

Asciidoctor::Extensions.register :git do

  doc = document

  inline_macro :linkgit do
    if doc.doctype == 'book'
      format = '<ulink url="%1$s.html">%1$s(%2$s)</ulink>'
    elsif doc.basebackend? 'html'
      prefix = doc.attr('git-relative-html-prefix')
      format = %(<a href="#{prefix}%1$s.html">%1$s(%2$s)</a>)
    elsif doc.basebackend? 'docbook'
      format = <<~EOF.chomp
      <citerefentry>
      <refentrytitle>%s</refentrytitle><manvolnum>%s</manvolnum>
      </citerefentry>
      EOF
    else
      return
    end

    process do |_, target, attrs|
      format % [target, attrs[1]]
    end
  end

end
