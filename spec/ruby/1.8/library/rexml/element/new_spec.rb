require 'rexml/document'
require File.dirname(__FILE__) + '/../../../spec_helper'

describe "REXML::Element#new" do

  it "creates an element from tag name" do
    REXML::Element.new("foo").name.should == "foo"
  end

  it "creates an element with the default attributes" do
    e = REXML::Element.new
    e.name.should == REXML::Element::UNDEFINED
    e.context.should == nil
    e.parent.should == nil
  end

  it "creates an element from another element" do
    e = REXML::Element.new "foo"
    f = REXML::Element.new e
    e.name.should == f.name
    e.context.should == f.context
    e.parent.should == f.parent
  end
  
  it "takes parent as second argument" do
    parent = REXML::Element.new "foo"
    child = REXML::Element.new "bar", parent
    child.parent.should == parent
  end

  it "takes context as third argument" do
    context = {"some_key" => "some_value"}
    REXML::Element.new("foo", nil, context).context.should == context
  end
end
