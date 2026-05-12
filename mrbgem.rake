MRuby::Gem::Specification.new('mruby-process') do |spec|
  spec.license = 'MIT'
  spec.authors = 'Internet Initiative Japan Inc.'
  spec.version = '0.1.0'
  spec.description = 'Process module for mruby'

  spec.add_dependency 'mruby-io'

  if ENV["ENV"] == "TEST"
    spec.add_dependency 'mruby-minitest', github: '0x1eef/mruby-minitest', branch: "main"
    spec.rbfiles.concat Dir[File.expand_path('spec/*.rb', __dir__)].sort
  end

  spec.rbfiles = Dir[File.expand_path('mrblib/*.rb', __dir__)].sort
end
